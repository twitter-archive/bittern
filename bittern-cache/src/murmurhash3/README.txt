MurmurHash3 was written by Austin Appleby, and is placed in the public
domain. The author hereby disclaims copyright to this source code.

Note - The x86 and x64 versions do _not_ produce the same results, as the
algorithms are optimized for their respective platforms. You can still
compile and run any of them on any platform, but your performance with the
non-native version will be less than optimal.

The API exposed in murmurhash3 uses uint28_t data type (which only supports
assignment and equality/inequality operators) as opposed to __int128 native
gcc type, as said type is not supported by all implementations of gcc.

Note that murmurhash is optimized for specific architecturesa and yields
different hash values depending on which flavor is chosen, x86 or x64.
In order to get reliable and repeatable results we have
arbitrarily chosen the x64 implementation for use in both x64 and x86
platforms. This choice is non-optimal for x86 platforms, but c'est la vie.
