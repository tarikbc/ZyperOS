CHECKS += check-logcap
#| Each zyperos.log held to about 2 MB (src/logcap.h): a 5 MB log as its boot's first 256 KB, a
#| marker with the count of lines dropped and its newest 1.5 MB, every line whole; trimmed again, the count adds up
