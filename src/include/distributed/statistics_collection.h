/*-------------------------------------------------------------------------
 *
 * statistics_collection.h
 *
 * Copyright (c) 2017, Citus Data, Inc.
 *
 *-------------------------------------------------------------------------
 */

#ifndef STATISTICS_COLLECTION_H
#define STATISTICS_COLLECTION_H

/* Config variables managed via guc.c */
extern bool EnableStatisticsCollection;

#if HAVE_LIBCURL

#define STATS_COLLECTION_HOST "https://citus-statistics.herokuapp.com"
#define HTTP_TIMEOUT_SECONDS 5

extern void WarnIfSyncDNS(void);
extern bool CollectBasicUsageStatistics(void);
extern void CheckForUpdates(void);

#endif /* HAVE_LIBCURL */

#endif /* STATISTICS_COLLECTION_H */
