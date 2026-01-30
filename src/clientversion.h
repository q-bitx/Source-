#ifndef BITCOIN_CLIENTVERSION_H
#define BITCOIN_CLIENTVERSION_H

#include <util/macros.h>
#include <bitcoin-build-config.h> // IWYU pragma: keep

#define COPYRIGHT_YEAR 2025
#define CLIENT_BUGREPORT "Please report bugs to https://qbitx.org/"

#define CLIENT_VERSION_MAJOR 0
#define CLIENT_VERSION_MINOR 1
#define CLIENT_VERSION_BUILD 0
#define CLIENT_VERSION_REVISION 1
#define CLIENT_VERSION_IS_RELEASE true

#if !defined(CLIENT_VERSION_MAJOR) || !defined(CLIENT_VERSION_MINOR) || !defined(CLIENT_VERSION_BUILD)
#error Client version information missing: version is not defined by bitcoin-build-config.h or in any other way
#endif

#include <string>
#include <vector>

static const int CLIENT_VERSION =
                             10000 * CLIENT_VERSION_MAJOR
                         +     100 * CLIENT_VERSION_MINOR
                         +       1 * CLIENT_VERSION_BUILD;

extern const std::string UA_NAME;

static const std::string CLIENT_NAME = "Q-BitX";
std::string FormatFullVersion();
std::string FormatSubVersion(const std::string& name, int nClientVersion, const std::vector<std::string>& comments);
std::string CopyrightHolders(const std::string& strPrefix);
/** Returns licensing information (for -version) */
std::string LicenseInfo();

#endif // BITCOIN_CLIENTVERSION_H
