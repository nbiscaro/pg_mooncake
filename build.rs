fn main() {
    println!("cargo:rustc-link-search=native=pg_duckdb");
    println!("cargo:rustc-link-lib=static=pg_duckdb");

    println!("cargo:rustc-link-search=native=pg_duckdb/third_party/duckdb/build/release");
    println!("cargo:rustc-link-lib=static=duckdb_bundle");

    println!("cargo:rustc-link-lib=dylib=crypto");
    println!("cargo:rustc-link-lib=dylib=ssl");
    println!("cargo:rustc-link-lib=dylib=stdc++");
}
