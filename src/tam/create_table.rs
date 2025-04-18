use crate::{error::Result, pgmoonlink};
use pgrx::prelude::*;
use std::fmt::Write;

#[pg_extern]
fn create_mooncake_table(name: String, table: String) -> i64 {
    create_table(name, table).unwrap()
}

fn create_table(name: String, table: String) -> Result<i64> {
    let schema = "public".to_owned();
    let query = get_create_table_query(&name, &table)?;
    Spi::run(query.as_str())?;
    pgmoonlink::create_table(schema, table)
}

fn get_create_table_query(name: &String, table: &String) ->Result<String> {
    let mut query = String::new();
    write!(query, "CREATE TABLE {} (", spi::quote_identifier(&name))?;
    write!(query, "a int, b text")?;
    write!(query, ") USING mooncake")?;
    Ok(query)
}
