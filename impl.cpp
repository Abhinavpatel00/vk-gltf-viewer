#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <vulkan/vulkan_hpp_macros.hpp>
#define WUFFS_IMPLEMENTATION
#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__AUX__BASE
#define WUFFS_CONFIG__MODULE__AUX__IMAGE
#define WUFFS_CONFIG__MODULE__BASE
#define WUFFS_CONFIG__MODULE__ADLER32
#define WUFFS_CONFIG__MODULE__CRC32
#define WUFFS_CONFIG__MODULE__DEFLATE
#define WUFFS_CONFIG__MODULE__ZLIB
#ifndef _MSC_VER
// Currently wuffs does not used for JPEG decoding in Windows.
// https://github.com/google/wuffs/issues/151
#define WUFFS_CONFIG__MODULE__JPEG
#endif
#define WUFFS_CONFIG__MODULE__PNG
#include <wuffs-unsupported-snapshot.c>

import vulkan_hpp;

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE