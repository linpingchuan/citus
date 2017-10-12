/* citus-7.0-15--7.0-16 */

CREATE OR REPLACE FUNCTION pg_catalog.citus_version()
    RETURNS text
    LANGUAGE C STABLE STRICT
    AS 'MODULE_PATHNAME', $$citus_version$$;
COMMENT ON FUNCTION pg_catalog.citus_version()
    IS 'Citus version string';
