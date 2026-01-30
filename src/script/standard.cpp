#include "base58.h"
#include "crypto/sha256.h"
#include "crypto/ripemd160.h"
#include "standard.h"
#include <hash.h>
#include <uint256.h>
#include "uint160.h"
//uint160 Hash160(const std::vector<unsigned char>& pubkey) {
  //  std::vector<unsigned char> sha256Hash = SHA256(pubkey);
    ///return RIPEMD160(sha256Hash);
//}

//std::string EncodePQAddress(const std::vector<unsigned char>& pubkey) {
    //uint160 hash = Hash160(pubkey);

    //std::vector<unsigned char> data;
    //data.insert(data.end(), hash.begin(), hash.end());

  //  return EncodeBase58Check(data);
//}

//bool DecodePQAddress(const std::string& str, std::vector<unsigned char>& hashOut) {
    //std::vector<unsigned char> data;
    //if (!DecodeBase58Check(str, data)) {
      //  return false;
    //}

    //if (data.size() != 21 || data[0] != 0x1E) {
     //   return false;
   // }

  //  hashOut.assign(data.begin() + 1, data.end());
  //  return true;
//}

//uint160 Hash160(const std::vector<unsigned char>& pubkey) {
  //  std::vector<unsigned char> sha256Hash = SHA256(pubkey);
    //return RIPEMD160(sha256Hash);
//}
uint160 Hash160(const std::vector<unsigned char>& pubkey) {
    uint256 sha256Hash;
    CSHA256().Write(pubkey.data(), pubkey.size()).Finalize(sha256Hash.begin());

    uint160 result;
    CRIPEMD160().Write(sha256Hash.begin(), sha256Hash.size()).Finalize(result.begin());
    return result;
}

std::string EncodePQAddress(const std::vector<unsigned char>& pubkey) {
    uint160 hash = Hash160(pubkey);

    std::vector<unsigned char> data;
    data.push_back(0x51);
    data.insert(data.end(), hash.begin(), hash.end());

    return EncodeBase58Check(data);
}

bool DecodePQAddress(const std::string& str, std::vector<unsigned char>& hashOut) {
    std::vector<unsigned char> data;
    if (!DecodeBase58Check(str, data, -1)) {
        return false;
    }

    if (data.size() != 21 || data[0] != 0x51) {
        return false;
    }

    hashOut.assign(data.begin() + 1, data.end());
    return true;
}
