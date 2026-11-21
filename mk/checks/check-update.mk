CHECKS += check-update
#| System Update end to end: two fake releases packed and signed with a throwaway key, check,
#| download, a cut download resumed, refused lists and packs, the boot switch and its step back
#| (skips without OpenSSL 3, libcurl or zlib)
