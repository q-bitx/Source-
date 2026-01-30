#include "dilithium_wrapper.h"
#include <boost/test/unit_test.hpp>
#include <vector>
#include <cstring>

#include "dilithium_wrapper.h"

// Dilithium params.h defines very generic macros (N, L, K, Q...) that break Boost MPL.
// Kill the worst offenders before including Boost.
#ifdef N
#undef N
#endif
#ifdef L
#undef L
#endif
#ifdef K
#undef K
#endif

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(dilithium_suite)

BOOST_AUTO_TEST_CASE(GenerateSignAndVerify)
{

    pqcrypto::dilithium::PQPublicKey pub{};
    pqcrypto::dilithium::PQPrivateKey priv{};
    pqcrypto::dilithium::keygen(&pub, &priv);

    bool pub_nonzero = false, priv_nonzero = false;
    for (auto b : pub.data)  if (b) { pub_nonzero = true; break; }
    for (auto b : priv.data) if (b) { priv_nonzero = true; break; }
    BOOST_CHECK(pub_nonzero);
    BOOST_CHECK(priv_nonzero);

    const char* message = "hello qbitx";
    std::vector<unsigned char> signature;

    BOOST_CHECK(pqcrypto::dilithium::sign(&priv, (const uint8_t*)message, std::strlen(message), signature));
    BOOST_CHECK(!signature.empty());
    BOOST_CHECK(pqcrypto::dilithium::verify(&pub, (const uint8_t*)message, std::strlen(message), signature));
}

BOOST_AUTO_TEST_CASE(PQCheckSig_NegativeTests)
{
    using namespace pqcrypto::dilithium;

    PQPublicKey pub{};
    PQPrivateKey priv{};
    keygen(&pub, &priv);

    const char* message = "qbitx-pqchecksigs";
    std::vector<unsigned char> sig;
    BOOST_REQUIRE(sign(&priv, (const uint8_t*)message, std::strlen(message), sig));
    BOOST_REQUIRE(verify(&pub, (const uint8_t*)message, std::strlen(message), sig));

    std::vector<unsigned char> sig_bad = sig;
    sig_bad[0] ^= 0x01;
    BOOST_CHECK(!verify(&pub, (const uint8_t*)message, std::strlen(message), sig_bad));


}

BOOST_AUTO_TEST_SUITE_END()
