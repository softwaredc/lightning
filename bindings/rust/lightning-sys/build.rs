use std::path::PathBuf;

fn main() {
    let manifest = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    // Try to link to an existing build
    let build_dir = manifest
        .join("../../../build")
        .canonicalize()
        .unwrap_or_else(|_| manifest.join("../../../build"));

    if build_dir.join("liblightning.so").exists() || build_dir.join("liblightning.a").exists() {
        println!("cargo:rustc-link-search=native={}", build_dir.display());
        println!("cargo:rustc-link-lib=lightning");
    } else {
        // Build from source
        cc::Build::new()
            .file("../../../src/lightning.c")
            .file("../../../src/chunked.c")
            .file("../../../src/url.c")
            .include("../../../include")
            .flag_if_supported("-O3")
            .flag_if_supported("-mavx2")
            .compile("lightning");
    }
    println!("cargo:rerun-if-changed=../../../src/lightning.c");
    println!("cargo:rerun-if-changed=../../../src/chunked.c");
    println!("cargo:rerun-if-changed=../../../src/url.c");
    println!("cargo:rerun-if-changed=../../../include/lightning/lightning.h");
}
