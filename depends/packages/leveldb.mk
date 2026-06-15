package=leveldb
$(package)_version=1.23
$(package)_download_path=https://github.com/google/leveldb/archive/refs/tags
$(package)_file_name=$($(package)_version).tar.gz
$(package)_sha256_hash=9a37f8a6174f09bd622bc723b55881dc541cd50747cbd08831c2a82d620f6d76
$(package)_build_subdir=build
$(package)_dependencies=

define $(package)_set_vars
  $(package)_config_opts := -DCMAKE_BUILD_TYPE=None
  $(package)_config_opts += -DLEVELDB_BUILD_TESTS=OFF -DLEVELDB_BUILD_BENCHMARKS=OFF
  $(package)_config_opts += -DBUILD_SHARED_LIBS=OFF
endef

$(package)_preprocess_cmds = sed -i '/-fno-rtti/d' CMakeLists.txt; sed -i '/REGEX REPLACE "-frtti"/d' CMakeLists.txt

define $(package)_config_cmds
  $($(package)_cmake) -S .. -B . $($(package)_config_opts)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef
