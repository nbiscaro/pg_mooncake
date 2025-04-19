use crate::error::{Error, Result};
use crate::pgmoonlink;
use pgrx::prelude::*;
use postgres::{Client, NoTls, SimpleQueryMessage};

#[pg_extern]
fn create_mooncake_table(table: &str, uri: &str) {
    create_table(table, uri).unwrap()
}

fn create_table(table: &str, uri: &str) -> Result<()> {
    let schema = "public";
    let mut client = Client::connect("postgresql://vscode@localhost:28817/pg_mooncake", NoTls)?;
    let create_table_query = format!(
        "CREATE TABLE {} ({}) USING mooncake",
        spi::quote_identifier(&table),
        get_columns(&mut client, schema, uri)?
    );
    client.simple_query(&create_table_query)?;
    pgmoonlink::create_table(schema.to_owned(), table.to_owned(), uri.to_owned())
}

fn get_columns(client: &mut Client, schema: &str, uri: &str) -> Result<String> {
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
        spi::quote_identifier(uri)
    );
    for message in client.simple_query(&get_columns_query)? {
        if let SimpleQueryMessage::Row(row) = message {
            return row
                .get("columns")
                .map(str::to_owned)
                .ok_or_else(|| Error::Internal("column 'columns' not found".to_owned()));
        }
    }
    Err(Error::Invalid(format!(
        "table '{}' not found",
        spi::quote_identifier(schema)
    )))
}
