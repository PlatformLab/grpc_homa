#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>

#include <vector>

#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>

#include "src/core/lib/surface/channel.h"

// This file contains miscellaneous small facilities that are useful
// in the Homa gRPC driver.

template<class P, class M>
size_t offsetOf(const M P::*member)
{
    return reinterpret_cast<size_t>(&( reinterpret_cast<P*>(0)->*member));
}

template<class P, class M>
P* containerOf(M* ptr, const M P::*member)
{
    return reinterpret_cast<P*>(reinterpret_cast<char*>(ptr)
            - offsetOf(member));
}

inline void parseType(const char *s, char **end, int *value)
{
	*value = strtol(s, end, 0);
}

inline void parseType(const char *s, char **end, int64_t *value)
{
	*value = strtoll(s, end, 0);
}

inline void parseType(const char *s, char **end, double *value)
{
	*value = strtod(s, end);
}

/**
 * Parse a value of a particular type from an argument word.
 * \param words
 *      Words of a command being parsed.
 * \param i
 *      Index within words of a word expected to contain an integer value
 *      (may be outside the range of words, in which case an error message
 *      is printed).
 * \param value
 *      The parsed value corresponding to @words[i] is stored here, if the
 *      function completes successfully.
 * \param option
 *      Name of option being parsed (for use in error messages).
 * \param typeName
 *      Human-readable name for ValueType (for use in error messages).
 *
 * \return
 *      Nonzero means success, zero means an error occurred (and a
 *      message was printed).
 */
template<typename ValueType>
int parse(std::vector<std::string> &words, unsigned i, ValueType *value,
		const char *option, const char *typeName)
{
	ValueType num;
	char *end;

	if (i >= words.size()) {
		printf("No value provided for %s\n", option);
		return 0;
	}
	parseType(words[i].c_str(), &end, &num);
	if (*end != 0) {
		printf("Bad value '%s' for %s; must be %s\n",
				words[i].c_str(), option, typeName);
		return 0;
	}
	*value = num;
	return 1;
}

extern void          fillData(void *data, int length, int firstValue);
extern void          logMetadata(const grpc_metadata_batch* mdBatch,
                        const char *info);
extern std::string   stringForStatus(grpc::Status *status);
extern std::string   stringPrintf(const char* format, ...)
                        __attribute__((format(printf, 1, 2)));
extern const char *  symbolForCode(grpc::StatusCode code);

#endif // UTIL_H