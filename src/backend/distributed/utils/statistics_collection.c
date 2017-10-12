/*-------------------------------------------------------------------------
 *
 * statistics_collection.c
 *	  Anonymous reports and statistics collection.
 *
 * Copyright (c) 2017, Citus Data, Inc.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

bool EnableStatisticsCollection = true; /* send basic usage statistics to Citus */

#ifdef HAVE_LIBCURL

#include <curl/curl.h>
#include <sys/utsname.h>

#include "access/xact.h"
#include "citus_version.h"
#include "distributed/metadata_cache.h"
#include "distributed/statistics_collection.h"
#include "distributed/worker_manager.h"
#include "lib/stringinfo.h"
#include "utils/builtins.h"
#include "utils/fmgrprotos.h"
#include "utils/json.h"

<<<<<<< HEAD
=======
bool EnableStatisticsCollection = true; /* send basic usage statistics to Citus */

#if HAVE_LIBCURL

static size_t CheckForUpdatesCallback(char *contents, size_t size, size_t count,
									  void *userData);
static bool JsonbFieldExists(Datum jsonb, const char *fieldName);
static int JsonbFieldInt(Datum jsonb, const char *fieldName);
static StringInfo JsonbFieldStr(Datum jsonb, const char *fieldName);
>>>>>>> Initial version of checking for updates.
static uint64 NextPow2(uint64 n);
static uint64 ClusterSize(List *distributedTableList);
static bool SendHttpPostJsonRequest(const char *url, const char *postFields, long
									timeoutSeconds, curl_write_callback responseCallback);

/* WarnIfSyncDNS warns if libcurl is compiled with synchronous DNS. */
void
WarnIfSyncDNS(void)
{
	curl_version_info_data *versionInfo = curl_version_info(CURLVERSION_NOW);
	if (!(versionInfo->features & CURL_VERSION_ASYNCHDNS))
	{
		ereport(WARNING, (errmsg("your current libcurl version doesn't support "
								 "asynchronous DNS, which might cause unexpected "
								 "delays in the operation of Citus"),
						  errhint("Install a libcurl version with asynchronous DNS "
								  "support.")));
	}
}


/*
 * CollectBasicUsageStatistics sends basic usage statistics to Citus servers.
 * This includes Citus version, table count rounded to next power of 2, cluster
 * size rounded to next power of 2, worker node count, and uname data. Returns
 * true if we actually have sent statistics to the server.
 */
bool
CollectBasicUsageStatistics(void)
{
	List *distributedTables = NIL;
	uint64 roundedDistTableCount = 0;
	uint64 roundedClusterSize = 0;
	uint32 workerNodeCount = 0;
	StringInfo fields = makeStringInfo();
	struct utsname unameData;
	memset(&unameData, 0, sizeof(unameData));

	StartTransactionCommand();
	distributedTables = DistributedTableList();
	roundedDistTableCount = NextPow2(list_length(distributedTables));
	roundedClusterSize = NextPow2(ClusterSize(distributedTables));
	workerNodeCount = ActivePrimaryNodeCount();
	CommitTransactionCommand();

	uname(&unameData);

	appendStringInfoString(fields, "{\"citus_version\": ");
	escape_json(fields, CITUS_VERSION);
	appendStringInfo(fields, ",\"table_count\": " UINT64_FORMAT, roundedDistTableCount);
	appendStringInfo(fields, ",\"cluster_size\": " UINT64_FORMAT, roundedClusterSize);
	appendStringInfo(fields, ",\"worker_node_count\": %u", workerNodeCount);
	appendStringInfoString(fields, ",\"os_name\": ");
	escape_json(fields, unameData.sysname);
	appendStringInfoString(fields, ",\"os_release\": ");
	escape_json(fields, unameData.release);
	appendStringInfoString(fields, ",\"hwid\": ");
	escape_json(fields, unameData.machine);
	appendStringInfoString(fields, "}");

	return SendHttpPostJsonRequest(STATS_COLLECTION_HOST "/v1/usage_reports",
								   fields->data, HTTP_TIMEOUT_SECONDS, NULL);
}


/* CheckForUpdates queries Citus servers for newer releases of Citus. */
void
CheckForUpdates(void)
{
	SendHttpPostJsonRequest(
		STATS_COLLECTION_HOST "/v1/releases/latest?flavor=community",
		NULL, HTTP_TIMEOUT_SECONDS, &CheckForUpdatesCallback);
}


/*
 * CheckForUpdatesCallback receives the response for the request sent by
 * CheckForUpdates(). It processes the response, and if there is a newer release
 * of Citus available, logs a LOG message. This function returns 0 if there are
 * any errors in the received response, which means we didn't consume the data.
 * Otherwise, it returns (size * count) which means we consumed all of the data.
 */
static size_t
CheckForUpdatesCallback(char *contents, size_t size, size_t count, void *userData)
{
	PG_TRY();
	{
		const int citusVersionMajor = CITUS_VERSION_NUM / 10000;
		const int citusVersionMinor = (CITUS_VERSION_NUM / 100) % 100;
		const int citusVersionPatch = CITUS_VERSION_NUM % 100;
		Datum responseJson = 0;
		StringInfo releaseVersion = NULL;
		int releaseMajor = 0;
		int releaseMinor = 0;
		int releasePatch = 0;
		char *updateType = NULL;

		StringInfo responseNullTerminated = makeStringInfo();
		appendBinaryStringInfo(responseNullTerminated, contents, size * count);
		responseJson = DirectFunctionCall1(jsonb_in,
										   CStringGetDatum(responseNullTerminated->data));

		if (!JsonbFieldExists(responseJson, "version") ||
			!JsonbFieldExists(responseJson, "major") ||
			!JsonbFieldExists(responseJson, "minor") ||
			!JsonbFieldExists(responseJson, "patch"))
		{
			return 0;
		}

		releaseVersion = JsonbFieldStr(responseJson, "version");
		releaseMajor = JsonbFieldInt(responseJson, "major");
		releaseMinor = JsonbFieldInt(responseJson, "minor");
		releasePatch = JsonbFieldInt(responseJson, "patch");

		if (releaseMajor > citusVersionMajor)
		{
			updateType = "major";
		}
		else if (releaseMajor == citusVersionMajor &&
				 releaseMinor > citusVersionMinor)
		{
			updateType = "minor";
		}
		else if (releaseMajor == citusVersionMajor &&
				 releaseMinor == citusVersionMinor &&
				 releasePatch > citusVersionPatch)
		{
			updateType = "patch";
		}

		if (updateType != NULL)
		{
			ereport(LOG, (errmsg("a newer %s release of Citus (%s) is available",
								 updateType, releaseVersion->data)));
		}

		return size * count;
	}
	PG_CATCH();
	{
		FlushErrorState();
		return 0;
	}
	PG_END_TRY();
}


/* JsonbFieldExists checks if the given field exists in the given JSONB object. */
static bool
JsonbFieldExists(Datum jsonb, const char *fieldName)
{
	return DatumGetBool(DirectFunctionCall2(jsonb_exists, jsonb,
											CStringGetTextDatum(fieldName)));
}


/*
 * JsonbFieldInt returns integer value of the given field in the given JSONB
 * object. Caller should have checked that the field exists before calling this
 * function. If field value cannot be converted to an integer, throws an error.
 */
static int
JsonbFieldInt(Datum jsonb, const char *fieldName)
{
	StringInfo fieldStr = JsonbFieldStr(jsonb, fieldName);
	return pg_atoi(fieldStr->data, fieldStr->len, '\0');
}


/*
 * JsonbFieldInt returns string value of the given field in the given JSONB
 * object. Caller should have checked that the field exists before calling this
 * function.
 */
static StringInfo
JsonbFieldStr(Datum jsonb, const char *fieldName)
{
	char *fieldCString = TextDatumGetCString(
		DirectFunctionCall2(jsonb_object_field_text, jsonb,
							CStringGetTextDatum(fieldName)));
	StringInfo fieldStr = makeStringInfo();
	appendStringInfoString(fieldStr, fieldCString);
	return fieldStr;
}


/*
 * ClusterSize returns total size of data store in the cluster consisting of
 * given distributed tables. We ignore tables which we cannot get their size.
 */
static uint64
ClusterSize(List *distributedTableList)
{
	uint64 clusterSize = 0;
	ListCell *distTableCacheEntryCell = NULL;

	foreach(distTableCacheEntryCell, distributedTableList)
	{
		DistTableCacheEntry *distTableCacheEntry = lfirst(distTableCacheEntryCell);
		Oid relationId = distTableCacheEntry->relationId;
		MemoryContext savedContext = CurrentMemoryContext;

		PG_TRY();
		{
			Datum distTableSizeDatum = DirectFunctionCall1(citus_table_size,
														   ObjectIdGetDatum(relationId));
			clusterSize += DatumGetInt64(distTableSizeDatum);
		}
		PG_CATCH();
		{
			FlushErrorState();

			/* citus_table_size() throws an error while the memory context is changed */
			MemoryContextSwitchTo(savedContext);
		}
		PG_END_TRY();
	}

	return clusterSize;
}


/*
 * NextPow2 returns smallest power of 2 less than or equal to n. If n is greater
 * than 2^63, it returns 2^63. Returns 0 when n is 0.
 */
static uint64
NextPow2(uint64 n)
{
	uint64 result = 1;

	if (n == 0)
	{
		return 0;
	}

	/* if there is no 64-bit power of 2 greater than n, return 2^63 */
	if (n > (1ull << 63))
	{
		return (1ull << 63);
	}

	while (result < n)
	{
		result *= 2;
	}

	return result;
}


/*
 * SendHttpPostJsonRequest sends a HTTP/HTTPS POST request to the given URL with
 * the given json object.
 */
static bool
SendHttpPostJsonRequest(const char *url, const char *jsonObj, long timeoutSeconds,
						curl_write_callback responseCallback)
{
	bool success = false;
	CURLcode curlCode = false;
	CURL *curl = NULL;

	curl_global_init(CURL_GLOBAL_DEFAULT);
	curl = curl_easy_init();
	if (curl)
	{
		struct curl_slist *headers = NULL;
		headers = curl_slist_append(headers, "Content-Type: application/json");
		headers = curl_slist_append(headers, "charsets: utf-8");

		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

		if (jsonObj != NULL)
		{
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonObj);
		}

		if (responseCallback != NULL)
		{
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, responseCallback);
		}

		curlCode = curl_easy_perform(curl);
		if (curlCode == CURLE_OK)
		{
			int64 httpCode = 0;
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
			if (httpCode == 200)
			{
				success = true;
			}
			else if (httpCode >= 400 && httpCode < 500)
			{
				ereport(WARNING, (errmsg("HTTP request failed."),
								  errhint("HTTP response code: " INT64_FORMAT,
										  httpCode)));
			}
		}
		else
		{
			ereport(WARNING, (errmsg("Sending HTTP POST request failed."),
							  errhint("Error code: %s.", curl_easy_strerror(curlCode))));
		}

		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);
	}

	curl_global_cleanup();

	return success;
}


#endif /* HAVE_LIBCURL */
