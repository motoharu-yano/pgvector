#include "postgres.h"

#include <float.h>
#include <math.h>

#include "halfutils.h"
#include "halfvec.h"
#include "ivfflat.h"
#include "miscadmin.h"
#include "utils/datum.h"
#include "utils/memutils.h"
#include "vector.h"

/*
 * Initialize with kmeans++
 *
 * https://theory.stanford.edu/~sergei/papers/kMeansPP-soda.pdf
 */
static void
InitCenters(Relation index, VectorArray samples, VectorArray centers, float *lowerBound)
{
	FmgrInfo   *procinfo;
	Oid			collation;
	int64		j;
	float	   *weight = palloc(samples->length * sizeof(float));
	int			numCenters = centers->maxlen;
	int			numSamples = samples->length;

	procinfo = index_getprocinfo(index, 1, IVFFLAT_KMEANS_DISTANCE_PROC);
	collation = index->rd_indcollation[0];

	/* Choose an initial center uniformly at random */
	VectorArraySet(centers, 0, VectorArrayGet(samples, RandomInt() % samples->length));
	centers->length++;

	for (j = 0; j < numSamples; j++)
		weight[j] = FLT_MAX;

	for (int i = 0; i < numCenters; i++)
	{
		double		sum;
		double		choice;

		CHECK_FOR_INTERRUPTS();

		sum = 0.0;

		for (j = 0; j < numSamples; j++)
		{
			Datum		vec = PointerGetDatum(VectorArrayGet(samples, j));
			double		distance;

			/* Only need to compute distance for new center */
			/* TODO Use triangle inequality to reduce distance calculations */
			distance = DatumGetFloat8(FunctionCall2Coll(procinfo, collation, vec, PointerGetDatum(VectorArrayGet(centers, i))));

			/* Set lower bound */
			lowerBound[j * numCenters + i] = distance;

			/* Use distance squared for weighted probability distribution */
			distance *= distance;

			if (distance < weight[j])
				weight[j] = distance;

			sum += weight[j];
		}

		/* Only compute lower bound on last iteration */
		if (i + 1 == numCenters)
			break;

		/* Choose new center using weighted probability distribution. */
		choice = sum * RandomDouble();
		for (j = 0; j < numSamples - 1; j++)
		{
			choice -= weight[j];
			if (choice <= 0)
				break;
		}

		VectorArraySet(centers, i + 1, VectorArrayGet(samples, j));
		centers->length++;
	}

	pfree(weight);
}

/*
 * Apply norm to vector
 */
static inline void
ApplyNorm(FmgrInfo *normprocinfo, Oid collation, Datum value, IvfflatType type)
{
	double		norm = DatumGetFloat8(FunctionCall1Coll(normprocinfo, collation, value));

	/* TODO Handle zero norm */
	if (norm > 0)
	{
		if (type == IVFFLAT_TYPE_VECTOR)
		{
			Vector	   *vec = DatumGetVector(value);

			for (int i = 0; i < vec->dim; i++)
				vec->x[i] /= norm;
		}
		else if (type == IVFFLAT_TYPE_HALFVEC)
		{
			HalfVector *vec = DatumGetHalfVector(value);

			for (int i = 0; i < vec->dim; i++)
				vec->x[i] = Float4ToHalfUnchecked(HalfToFloat4(vec->x[i]) / norm);
		}
		else
			elog(ERROR, "Unsupported type");
	}
}

/*
 * Compare vectors
 */
static int
CompareVectors(const void *a, const void *b)
{
	return vector_cmp_internal((Vector *) a, (Vector *) b);
}

/*
 * Compare half vectors
 */
static int
CompareHalfVectors(const void *a, const void *b)
{
	return halfvec_cmp_internal((HalfVector *) a, (HalfVector *) b);
}

/*
 * Quick approach if we have little data
 */
static void
QuickCenters(Relation index, VectorArray samples, VectorArray centers, IvfflatType type)
{
	int			dimensions = centers->dim;
	Oid			collation = index->rd_indcollation[0];
	FmgrInfo   *normprocinfo = IvfflatOptionalProcInfo(index, IVFFLAT_KMEANS_NORM_PROC);

	/* Copy existing vectors while avoiding duplicates */
	if (samples->length > 0)
	{
		if (type == IVFFLAT_TYPE_VECTOR)
			qsort(samples->items, samples->length, samples->itemsize, CompareVectors);
		else if (type == IVFFLAT_TYPE_HALFVEC)
			qsort(samples->items, samples->length, samples->itemsize, CompareHalfVectors);
		else
			elog(ERROR, "Unsupported type");

		for (int i = 0; i < samples->length; i++)
		{
			Datum		vec = PointerGetDatum(VectorArrayGet(samples, i));

			if (i == 0 || !datumIsEqual(vec, PointerGetDatum(VectorArrayGet(samples, i - 1)), false, -1))
			{
				VectorArraySet(centers, centers->length, DatumGetPointer(vec));
				centers->length++;
			}
		}
	}

	/* Fill remaining with random data */
	while (centers->length < centers->maxlen)
	{
		Datum		center = PointerGetDatum(VectorArrayGet(centers, centers->length));

		if (type == IVFFLAT_TYPE_VECTOR)
		{
			Vector	   *vec = DatumGetVector(center);

			SET_VARSIZE(vec, VECTOR_SIZE(dimensions));
			vec->dim = dimensions;

			for (int j = 0; j < dimensions; j++)
				vec->x[j] = RandomDouble();
		}
		else if (type == IVFFLAT_TYPE_HALFVEC)
		{
			HalfVector *vec = DatumGetHalfVector(center);

			SET_VARSIZE(vec, HALFVEC_SIZE(dimensions));
			vec->dim = dimensions;

			for (int j = 0; j < dimensions; j++)
				vec->x[j] = Float4ToHalfUnchecked((float) RandomDouble());
		}
		else
			elog(ERROR, "Unsupported type");

		/* Normalize if needed (only needed for random centers) */
		if (normprocinfo != NULL)
			ApplyNorm(normprocinfo, collation, center, type);

		centers->length++;
	}
}

#ifdef IVFFLAT_MEMORY
/*
 * Show memory usage
 */
static void
ShowMemoryUsage(MemoryContext context, Size estimatedSize)
{
#if PG_VERSION_NUM >= 130000
	elog(INFO, "total memory: %zu MB",
		 MemoryContextMemAllocated(context, true) / (1024 * 1024));
#else
	MemoryContextStats(context);
#endif
	elog(INFO, "estimated memory: %zu MB", estimatedSize / (1024 * 1024));
}
#endif

/*
 * Compute new centers
 */
static void
ComputeNewCenters(VectorArray samples, VectorArray aggCenters, VectorArray newCenters, int *centerCounts, int *closestCenters, FmgrInfo *normprocinfo, Oid collation, IvfflatType type)
{
	int			dimensions = aggCenters->dim;
	int			numCenters = aggCenters->maxlen;
	int			numSamples = samples->length;

	/* Reset sum and count */
	for (int j = 0; j < numCenters; j++)
	{
		Vector	   *vec = (Vector *) VectorArrayGet(aggCenters, j);

		for (int k = 0; k < dimensions; k++)
			vec->x[k] = 0.0;

		centerCounts[j] = 0;
	}

	/* Increment sum of closest center */
	if (type == IVFFLAT_TYPE_VECTOR)
	{
		for (int j = 0; j < numSamples; j++)
		{
			Vector	   *aggCenter = (Vector *) VectorArrayGet(aggCenters, closestCenters[j]);
			Vector	   *vec = (Vector *) VectorArrayGet(samples, j);

			for (int k = 0; k < dimensions; k++)
				aggCenter->x[k] += vec->x[k];
		}
	}
	else if (type == IVFFLAT_TYPE_HALFVEC)
	{
		for (int j = 0; j < numSamples; j++)
		{
			Vector	   *aggCenter = (Vector *) VectorArrayGet(aggCenters, closestCenters[j]);
			HalfVector *vec = (HalfVector *) VectorArrayGet(samples, j);

			for (int k = 0; k < dimensions; k++)
				aggCenter->x[k] += HalfToFloat4(vec->x[k]);
		}
	}
	else
		elog(ERROR, "Unsupported type");

	/* Increment count of closest center */
	for (int j = 0; j < numSamples; j++)
		centerCounts[closestCenters[j]] += 1;

	/* Divide sum by count */
	for (int j = 0; j < numCenters; j++)
	{
		Vector	   *vec = (Vector *) VectorArrayGet(aggCenters, j);

		if (centerCounts[j] > 0)
		{
			/* Double avoids overflow, but requires more memory */
			/* TODO Update bounds */
			for (int k = 0; k < dimensions; k++)
			{
				if (isinf(vec->x[k]))
					vec->x[k] = vec->x[k] > 0 ? FLT_MAX : -FLT_MAX;
			}

			for (int k = 0; k < dimensions; k++)
				vec->x[k] /= centerCounts[j];
		}
		else
		{
			/* TODO Handle empty centers properly */
			for (int k = 0; k < dimensions; k++)
				vec->x[k] = RandomDouble();
		}
	}

	/* Set new centers if different from agg centers */
	if (type == IVFFLAT_TYPE_HALFVEC)
	{
		for (int j = 0; j < numCenters; j++)
		{
			Vector	   *aggCenter = (Vector *) VectorArrayGet(aggCenters, j);
			HalfVector *newCenter = (HalfVector *) VectorArrayGet(newCenters, j);

			for (int k = 0; k < dimensions; k++)
				newCenter->x[k] = Float4ToHalfUnchecked(aggCenter->x[k]);
		}
	}

	/* Normalize if needed */
	if (normprocinfo != NULL)
	{
		for (int j = 0; j < numCenters; j++)
		{
			Datum		newCenter = PointerGetDatum(VectorArrayGet(newCenters, j));

			ApplyNorm(normprocinfo, collation, newCenter, type);
		}
	}
}

/*
 * Use Elkan for performance. This requires distance function to satisfy triangle inequality.
 *
 * We use L2 distance for L2 (not L2 squared like index scan)
 * and angular distance for inner product and cosine distance
 *
 * https://www.aaai.org/Papers/ICML/2003/ICML03-022.pdf
 */
static void
ElkanKmeans(Relation index, VectorArray samples, VectorArray centers, IvfflatType type)
{
	FmgrInfo   *procinfo;
	FmgrInfo   *normprocinfo;
	Oid			collation;
	int			dimensions = centers->dim;
	int			numCenters = centers->maxlen;
	int			numSamples = samples->length;
	VectorArray newCenters;
	VectorArray aggCenters;
	int		   *centerCounts;
	int		   *closestCenters;
	float	   *lowerBound;
	float	   *upperBound;
	float	   *s;
	float	   *halfcdist;
	float	   *newcdist;
	MemoryContext kmeansCtx;
	MemoryContext oldCtx;

	/* Calculate allocation sizes */
	Size		samplesSize = VECTOR_ARRAY_SIZE(samples->maxlen, samples->itemsize);
	Size		centersSize = VECTOR_ARRAY_SIZE(centers->maxlen, centers->itemsize);
	Size		newCentersSize = VECTOR_ARRAY_SIZE(numCenters, centers->itemsize);
	Size		aggCentersSize = type == IVFFLAT_TYPE_VECTOR ? 0 : VECTOR_ARRAY_SIZE(numCenters, VECTOR_SIZE(dimensions));
	Size		centerCountsSize = sizeof(int) * numCenters;
	Size		closestCentersSize = sizeof(int) * numSamples;
	Size		lowerBoundSize = sizeof(float) * numSamples * numCenters;
	Size		upperBoundSize = sizeof(float) * numSamples;
	Size		sSize = sizeof(float) * numCenters;
	Size		halfcdistSize = sizeof(float) * numCenters * numCenters;
	Size		newcdistSize = sizeof(float) * numCenters;

	/* Calculate total size */
	Size		totalSize = samplesSize + centersSize + newCentersSize + aggCentersSize + centerCountsSize + closestCentersSize + lowerBoundSize + upperBoundSize + sSize + halfcdistSize + newcdistSize;

	/* Check memory requirements */
	/* Add one to error message to ceil */
	if (totalSize > (Size) maintenance_work_mem * 1024L)
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("memory required is %zu MB, maintenance_work_mem is %d MB",
						totalSize / (1024 * 1024) + 1, maintenance_work_mem / 1024)));

	/* Ensure indexing does not overflow */
	if (numCenters * numCenters > INT_MAX)
		elog(ERROR, "Indexing overflow detected. Please report a bug.");

	/* Set support functions */
	procinfo = index_getprocinfo(index, 1, IVFFLAT_KMEANS_DISTANCE_PROC);
	normprocinfo = IvfflatOptionalProcInfo(index, IVFFLAT_KMEANS_NORM_PROC);
	collation = index->rd_indcollation[0];

	/* Use memory context */
	kmeansCtx = AllocSetContextCreate(CurrentMemoryContext,
									  "Ivfflat kmeans temporary context",
									  ALLOCSET_DEFAULT_SIZES);
	oldCtx = MemoryContextSwitchTo(kmeansCtx);

	/* Allocate space */
	/* Use float instead of double to save memory */
	centerCounts = palloc(centerCountsSize);
	closestCenters = palloc(closestCentersSize);
	lowerBound = palloc_extended(lowerBoundSize, MCXT_ALLOC_HUGE);
	upperBound = palloc(upperBoundSize);
	s = palloc(sSize);
	halfcdist = palloc_extended(halfcdistSize, MCXT_ALLOC_HUGE);
	newcdist = palloc(newcdistSize);

	aggCenters = VectorArrayInit(numCenters, dimensions, VECTOR_SIZE(dimensions));
	for (int j = 0; j < numCenters; j++)
	{
		Vector	   *vec = (Vector *) VectorArrayGet(aggCenters, j);

		SET_VARSIZE(vec, VECTOR_SIZE(dimensions));
		vec->dim = dimensions;
	}

	if (type == IVFFLAT_TYPE_VECTOR)
	{
		/* Use same centers to save memory */
		newCenters = aggCenters;
	}
	else if (type == IVFFLAT_TYPE_HALFVEC)
	{
		newCenters = VectorArrayInit(numCenters, dimensions, centers->itemsize);

		for (int j = 0; j < numCenters; j++)
		{
			HalfVector *vec = (HalfVector *) VectorArrayGet(newCenters, j);

			SET_VARSIZE(vec, HALFVEC_SIZE(dimensions));
			vec->dim = dimensions;
		}
	}
	else
		elog(ERROR, "Unsupported type");

#ifdef IVFFLAT_MEMORY
	ShowMemoryUsage(oldCtx, totalSize);
#endif

	/* Pick initial centers */
	InitCenters(index, samples, centers, lowerBound);

	/* Assign each x to its closest initial center c(x) = argmin d(x,c) */
	for (int64 j = 0; j < numSamples; j++)
	{
		float		minDistance = FLT_MAX;
		int			closestCenter = 0;

		/* Find closest center */
		for (int64 k = 0; k < numCenters; k++)
		{
			/* TODO Use Lemma 1 in k-means++ initialization */
			float		distance = lowerBound[j * numCenters + k];

			if (distance < minDistance)
			{
				minDistance = distance;
				closestCenter = k;
			}
		}

		upperBound[j] = minDistance;
		closestCenters[j] = closestCenter;
	}

	/* Give 500 iterations to converge */
	for (int iteration = 0; iteration < 500; iteration++)
	{
		int			changes = 0;
		bool		rjreset;

		/* Can take a while, so ensure we can interrupt */
		CHECK_FOR_INTERRUPTS();

		/* Step 1: For all centers, compute distance */
		for (int64 j = 0; j < numCenters; j++)
		{
			Datum		vec = PointerGetDatum(VectorArrayGet(centers, j));

			for (int64 k = j + 1; k < numCenters; k++)
			{
				float		distance = 0.5 * DatumGetFloat8(FunctionCall2Coll(procinfo, collation, vec, PointerGetDatum(VectorArrayGet(centers, k))));

				halfcdist[j * numCenters + k] = distance;
				halfcdist[k * numCenters + j] = distance;
			}
		}

		/* For all centers c, compute s(c) */
		for (int64 j = 0; j < numCenters; j++)
		{
			float		minDistance = FLT_MAX;

			for (int64 k = 0; k < numCenters; k++)
			{
				float		distance;

				if (j == k)
					continue;

				distance = halfcdist[j * numCenters + k];
				if (distance < minDistance)
					minDistance = distance;
			}

			s[j] = minDistance;
		}

		rjreset = iteration != 0;

		for (int64 j = 0; j < numSamples; j++)
		{
			bool		rj;

			/* Step 2: Identify all points x such that u(x) <= s(c(x)) */
			if (upperBound[j] <= s[closestCenters[j]])
				continue;

			rj = rjreset;

			for (int64 k = 0; k < numCenters; k++)
			{
				Datum		vec;
				float		dxcx;

				/* Step 3: For all remaining points x and centers c */
				if (k == closestCenters[j])
					continue;

				if (upperBound[j] <= lowerBound[j * numCenters + k])
					continue;

				if (upperBound[j] <= halfcdist[closestCenters[j] * numCenters + k])
					continue;

				vec = PointerGetDatum(VectorArrayGet(samples, j));

				/* Step 3a */
				if (rj)
				{
					dxcx = DatumGetFloat8(FunctionCall2Coll(procinfo, collation, vec, PointerGetDatum(VectorArrayGet(centers, closestCenters[j]))));

					/* d(x,c(x)) computed, which is a form of d(x,c) */
					lowerBound[j * numCenters + closestCenters[j]] = dxcx;
					upperBound[j] = dxcx;

					rj = false;
				}
				else
					dxcx = upperBound[j];

				/* Step 3b */
				if (dxcx > lowerBound[j * numCenters + k] || dxcx > halfcdist[closestCenters[j] * numCenters + k])
				{
					float		dxc = DatumGetFloat8(FunctionCall2Coll(procinfo, collation, vec, PointerGetDatum(VectorArrayGet(centers, k))));

					/* d(x,c) calculated */
					lowerBound[j * numCenters + k] = dxc;

					if (dxc < dxcx)
					{
						closestCenters[j] = k;

						/* c(x) changed */
						upperBound[j] = dxc;

						changes++;
					}
				}
			}
		}

		/* Step 4: For each center c, let m(c) be mean of all points assigned */
		ComputeNewCenters(samples, aggCenters, newCenters, centerCounts, closestCenters, normprocinfo, collation, type);

		/* Step 5 */
		for (int j = 0; j < numCenters; j++)
			newcdist[j] = DatumGetFloat8(FunctionCall2Coll(procinfo, collation, PointerGetDatum(VectorArrayGet(centers, j)), PointerGetDatum(VectorArrayGet(newCenters, j))));

		for (int64 j = 0; j < numSamples; j++)
		{
			for (int64 k = 0; k < numCenters; k++)
			{
				float		distance = lowerBound[j * numCenters + k] - newcdist[k];

				if (distance < 0)
					distance = 0;

				lowerBound[j * numCenters + k] = distance;
			}
		}

		/* Step 6 */
		/* We reset r(x) before Step 3 in the next iteration */
		for (int j = 0; j < numSamples; j++)
			upperBound[j] += newcdist[closestCenters[j]];

		/* Step 7 */
		for (int j = 0; j < numCenters; j++)
			VectorArraySet(centers, j, VectorArrayGet(newCenters, j));

		if (changes == 0 && iteration != 0)
			break;
	}

	MemoryContextSwitchTo(oldCtx);
	MemoryContextDelete(kmeansCtx);
}

/*
 * Detect issues with centers
 */
static void
CheckCenters(Relation index, VectorArray centers, IvfflatType type)
{
	FmgrInfo   *normprocinfo;

	if (centers->length != centers->maxlen)
		elog(ERROR, "Not enough centers. Please report a bug.");

	/* Ensure no NaN or infinite values */
	for (int i = 0; i < centers->length; i++)
	{
		if (type == IVFFLAT_TYPE_VECTOR)
		{
			Vector	   *vec = (Vector *) VectorArrayGet(centers, i);

			for (int j = 0; j < vec->dim; j++)
			{
				if (isnan(vec->x[j]))
					elog(ERROR, "NaN detected. Please report a bug.");

				if (isinf(vec->x[j]))
					elog(ERROR, "Infinite value detected. Please report a bug.");
			}
		}
		else if (type == IVFFLAT_TYPE_HALFVEC)
		{
			HalfVector *vec = (HalfVector *) VectorArrayGet(centers, i);

			for (int j = 0; j < vec->dim; j++)
			{
				if (HalfIsNan(vec->x[j]))
					elog(ERROR, "NaN detected. Please report a bug.");

				if (HalfIsInf(vec->x[j]))
					elog(ERROR, "Infinite value detected. Please report a bug.");
			}
		}
		else
			elog(ERROR, "Unsupported type");
	}

	/* Ensure no duplicate centers */
	/* Fine to sort in-place */
	if (type == IVFFLAT_TYPE_VECTOR)
		qsort(centers->items, centers->length, centers->itemsize, CompareVectors);
	else if (type == IVFFLAT_TYPE_HALFVEC)
		qsort(centers->items, centers->length, centers->itemsize, CompareHalfVectors);
	else
		elog(ERROR, "Unsupported type");

	for (int i = 1; i < centers->length; i++)
	{
		if (datumIsEqual(PointerGetDatum(VectorArrayGet(centers, i)), PointerGetDatum(VectorArrayGet(centers, i - 1)), false, -1))
			elog(ERROR, "Duplicate centers detected. Please report a bug.");
	}

	/* Ensure no zero vectors for cosine distance */
	/* Check NORM_PROC instead of KMEANS_NORM_PROC */
	normprocinfo = IvfflatOptionalProcInfo(index, IVFFLAT_NORM_PROC);
	if (normprocinfo != NULL)
	{
		Oid			collation = index->rd_indcollation[0];

		for (int i = 0; i < centers->length; i++)
		{
			double		norm = DatumGetFloat8(FunctionCall1Coll(normprocinfo, collation, PointerGetDatum(VectorArrayGet(centers, i))));

			if (norm == 0)
				elog(ERROR, "Zero norm detected. Please report a bug.");
		}
	}
}

/*
 * Perform naive k-means centering
 * We use spherical k-means for inner product and cosine
 */
void
IvfflatKmeans(Relation index, VectorArray samples, VectorArray centers, IvfflatType type)
{
	if (samples->length <= centers->maxlen)
		QuickCenters(index, samples, centers, type);
	else
		ElkanKmeans(index, samples, centers, type);

	CheckCenters(index, centers, type);
}
