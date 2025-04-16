use pgrx::prelude::*;

#[pg_extern]
fn create_mooncake_table(name: &str, table: &str) {
    // TODO
    create_mooncake_table_full(
        name,
        "localhost",
        28817,
        "pg_mooncake",
        "vscode",
        None,
        "public",
        table,
    )
}

#[pg_extern(name = "create_mooncake_table")]
fn create_mooncake_table_full(
    name: &str,
    host: &str,
    port: i32,
    database: &str,
    username: &str,
    password: Option<String>,
    schema: &str,
    table: &str,
) {
    let port = u16::try_from(port).unwrap();
    let _ = format!(
        "{} {} {} {} {} {:?} {} {}",
        name, host, port, database, username, password, schema, table
    );
}

// #[pg_extern]
// fn drop_mooncake_table(name: &str) {

// }

// fn create_mooncake_table_impl(
//     name: &str,
//     host: &str,
//     port: i32,
//     database: &str,
//     username: &str,
//     password: Option<String>,
//     schema: &str,
//     table: &str,
// ) -> Result<()> {
//     let port = u16::try_from(port)?;
//     Ok(())
// }
