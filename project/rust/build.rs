fn main() {
    embuild::build::LinkArgs::output_propagated("ESP_IDF").unwrap();
    embuild::build::CfgArgs::output_propagated("ESP_IDF").ok();
    println!("cargo:rerun-if-changed=build.rs");
}
