#pragma once

#include <istream>
#include <string>
#include <string_view>
#ifdef __cplusplus
extern "C" {
#endif
#include <openssl/evp.h>
#ifdef __cplusplus
}
#endif

namespace http_client{

constexpr size_t HASHER_BUFFER_SIZE = 4096;

// To add a new algorithm, just add a line here
#define HASH_ALGORITHMS(HASH_ALGORITHM) \
	HASH_ALGORITHM(md5, EVP_md5) \
	HASH_ALGORITHM(sha1, EVP_sha1) \
	HASH_ALGORITHM(sha224, EVP_sha224) \
	HASH_ALGORITHM(sha256, EVP_sha256) \
	HASH_ALGORITHM(sha384, EVP_sha384) \
	HASH_ALGORITHM(sha512, EVP_sha512) \
	HASH_ALGORITHM(sha512_224, EVP_sha512_224) \
	HASH_ALGORITHM(sha512_256, EVP_sha512_256) \
	HASH_ALGORITHM(sha3_224, EVP_sha3_224) \
	HASH_ALGORITHM(sha3_256, EVP_sha3_256) \
	HASH_ALGORITHM(sha3_384, EVP_sha3_384) \
	HASH_ALGORITHM(sha3_512, EVP_sha3_512) \
	HASH_ALGORITHM(blake2s256, EVP_blake2s256) \
	HASH_ALGORITHM(blake2b512, EVP_blake2b512) \
	HASH_ALGORITHM(ripemd160, EVP_ripemd160) \
	HASH_ALGORITHM(sm3, EVP_sm3)

class Hash {
/*
NOT THREAD-SAFE!!!
*/
public:
	explicit Hash(const EVP_MD* md);
	~Hash();

	// Declare factory and helper methods for each algorithm using X-macro
#define HASH_DECLARE(name, evp_func) \
	static Hash name(); \
	static std::string name(const void* data, std::size_t len); \
	static std::string name(const std::string_view& data);

	HASH_ALGORITHMS(HASH_DECLARE)

#undef HASH_DECLARE

	// Copyable and movable
	Hash(const Hash& other);
	Hash& operator=(const Hash& other);
	Hash(Hash&& other) noexcept;
	Hash& operator=(Hash&& other) noexcept;

	Hash& update(const void* data, std::size_t len);
	Hash& update(const std::string_view& data);

	void reset();
	void operator<<(std::istream& stream);
	std::string final();

	static std::string hexdigest(const std::string& bin_hash);

private:
	bool finalized_;
	std::string cached_result_;
	const EVP_MD* md_ = nullptr;
	EVP_MD_CTX* ctx_ = nullptr;
};

}
