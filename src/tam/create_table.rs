use crate::error::Result;
use crate::pgmoonlink;
use pgrx::prelude::*;
use postgres::{Client, NoTls};

#[pg_extern]
fn create_mooncake_table(dst: &str, src: &str) {
    // TODO: allow uri
    let uri = "postgresql://vscode@localhost:28817/pg_mooncake";
    create_table(dst, src, uri).unwrap()
}

fn create_table(dst: &str, src: &str, uri: &str) -> Result<()> {
    // TODO: allow schema in dst and src
    let schema = "public";
    // TODO: uri is only for src
    let mut client = Client::connect(uri, NoTls)?;

    let get_columns_query = format!(
        "SELECT string_agg(
            format(
                '%I %s%s',
                a.attname,
                format_type(a.atttypid, a.atttypmod),
                CASE WHEN a.attnotnull THEN ' NOT NULL' ELSE '' END
            ),
            ', ' ORDER BY a.attnum
        ) AS columns
        FROM pg_namespace n
        JOIN pg_class c ON c.relnamespace = n.oid
        JOIN pg_attribute a ON a.attrelid = c.oid
        WHERE n.nspname = '{}'
          AND c.relname = '{}'
          AND a.attnum > 0
          AND NOT a.attisdropped",
        spi::quote_identifier(schema),
        spi::quote_identifier(src)
    );
    let columns: String = client.query_one(&get_columns_query, &[])?.get(0);

    let create_table_query = format!(
        "CREATE TABLE {}.{} ({}) USING mooncake",
        spi::quote_identifier(schema),
        spi::quote_identifier(dst),
        columns
    );
    client.simple_query(&create_table_query)?;

    let get_table_id_query = format!(
        "SELECT '{}.{}'::regclass::oid",
        spi::quote_identifier(schema),
        spi::quote_identifier(dst),
    );
    let table_id = client.query_one(&get_table_id_query, &[])?.get(0);

    let database_id = unsafe { pgrx::pg_sys::MyDatabaseId.to_u32() };
    pgmoonlink::create_table(database_id, table_id, schema.to_owned(), src.to_owned())
}
