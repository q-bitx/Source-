#pragma once

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <crypto/dilithium_wrapper.h> // pqcrypto::dilithium::PUBKEY_BYTES
#include <hash.h> // Hash160
#include <span.h> // std::span
#include <util/hash_type.h> // uint160

class CDilithiumPubKey
{
public:
    static constexpr size_t SIZE = pqcrypto::dilithium::PUBKEY_BYTES;

    CDilithiumPubKey() : m_valid(false)
    {
        m_data.fill(0);
    }

    explicit CDilithiumPubKey(const std::vector<unsigned char>& v) : m_valid(false)
    {
        if (v.size() == SIZE) {
            std::copy(v.begin(), v.end(), m_data.begin());
            m_valid = true;
        } else {
            m_data.fill(0);
        }
    }

    bool IsValid() const { return m_valid; }

    size_t size() const { return SIZE; }

    const unsigned char* data() const { return m_data.data(); }
    unsigned char* data() { return m_data.data(); }

    const unsigned char* begin() const { return m_data.begin(); }
    unsigned char* begin() { return m_data.begin(); }

    const unsigned char* end() const { return m_data.end(); }
    unsigned char* end() { return m_data.end(); }

    uint160 GetID() const
    {
        return Hash160(std::span{m_data});
    }

    friend bool operator==(const CDilithiumPubKey& a, const CDilithiumPubKey& b)
    {
        return a.m_data == b.m_data;
    }

    friend bool operator<(const CDilithiumPubKey& a, const CDilithiumPubKey& b)
    {
        return std::lexicographical_compare(a.m_data.begin(), a.m_data.end(),
                                            b.m_data.begin(), b.m_data.end());
    }

private:
    std::array<unsigned char, SIZE> m_data;
    bool m_valid;
};

class CDilithiumKey
{
public:
    static constexpr size_t SIZE = pqcrypto::dilithium::PRIVKEY_BYTES;

    CDilithiumKey() : m_valid(false)
    {
        m_data.fill(0);
    }

    explicit CDilithiumKey(const std::vector<unsigned char>& v) : m_valid(false)
    {
        if (v.size() == SIZE) {
            std::copy(v.begin(), v.end(), m_data.begin());
            m_valid = true;
        } else {
            m_data.fill(0);
        }
    }

    bool IsValid() const { return m_valid; }

    size_t size() const { return SIZE; }

    const unsigned char* data() const { return m_data.data(); }
    unsigned char* data() { return m_data.data(); }

    const unsigned char* begin() const { return m_data.begin(); }
    unsigned char* begin() { return m_data.begin(); }

    const unsigned char* end() const { return m_data.end(); }
    unsigned char* end() { return m_data.end(); }

    std::vector<unsigned char> GetBytes() const
    {
        return std::vector<unsigned char>(m_data.begin(), m_data.end());
    }

private:
    std::array<unsigned char, SIZE> m_data;
    bool m_valid;
};

inline bool IsValidDilithiumPubKey(const std::vector<unsigned char>& pubkey)
{
    return pubkey.size() == CDilithiumPubKey::SIZE;
}

inline bool IsValidDilithiumKey(const std::vector<unsigned char>& key)
{
    return key.size() == CDilithiumKey::SIZE;
}
