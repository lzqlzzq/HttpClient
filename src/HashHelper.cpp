#include "HashHelper.hpp"

#include <stdexcept>
#include <cstdint>
#include <utility>

namespace http_client {

Hash::Hash(const EVP_MD* md) : md_(md), ctx_(EVP_MD_CTX_new()), finalized_(false) {
	if (!md_) throw std::invalid_argument("Digest: null EVP_MD");
	if (!ctx_) throw std::runtime_error("Digest: EVP_MD_CTX_new failed");
	if (EVP_DigestInit_ex(ctx_, md_, nullptr) != 1) {
		EVP_MD_CTX_free(ctx_);
		throw std::runtime_error("Digest: EVP_DigestInit_ex failed");
	}
}

Hash::~Hash() {
	if (this->ctx_) EVP_MD_CTX_free(this->ctx_);
}

// Define factory and helper methods
#define HASH_DECLARE(name, evp_func) \
	Hash Hash::name() { return Hash(evp_func()); } \
	std::string Hash::name(const void* data, std::size_t len) { \
		return Hash(evp_func()).update(data, len).final(); \
	} \
	std::string Hash::name(const std::string_view& data) { \
		return name(data.data(), data.size()); \
	}

HASH_ALGORITHMS(HASH_DECLARE)

#undef HASH_DECLARE

// Copy constructor
Hash::Hash(const Hash& other)
	: md_(other.md_), finalized_(other.finalized_), cached_result_(other.cached_result_) {
	this->reset();
	if (EVP_MD_CTX_copy_ex(this->ctx_, other.ctx_) != 1) {
		throw std::runtime_error("Digest: EVP_MD_CTX_copy_ex failed");
	}
}

// Copy assignment operator
Hash& Hash::operator=(const Hash& other) {
	if (this != &other) {
		this->reset();
		if (EVP_MD_CTX_copy_ex(this->ctx_, other.ctx_) != 1) {
			throw std::runtime_error("Digest: EVP_MD_CTX_copy_ex failed");
		}
		this->md_ = other.md_;
		this->finalized_ = other.finalized_;
		this->cached_result_ = other.cached_result_;
	}
	return *this;
}

// Move constructor
Hash::Hash(Hash&& other) noexcept
	: finalized_(std::exchange(other.finalized_, false)),
	  cached_result_(std::move(other.cached_result_)),
	  md_(std::exchange(other.md_, nullptr)) {
	EVP_MD_CTX_free(this->ctx_);
	this->ctx_ = std::exchange(other.ctx_, nullptr);
}

// Move assignment operator
Hash& Hash::operator=(Hash&& other) noexcept {
	if (this != &other) {
		EVP_MD_CTX_free(ctx_);
		this->ctx_ = std::exchange(other.ctx_, nullptr);
		this->md_ = std::exchange(other.md_, nullptr);
		this->finalized_ = std::exchange(other.finalized_, nullptr);
		this->cached_result_ = std::move(other.cached_result_);
	}
	return *this;
}

Hash& Hash::update(const void* data, std::size_t len) {
	if (EVP_DigestUpdate(ctx_, data, len) != 1)
		throw std::runtime_error("Digest: EVP_DigestUpdate failed");
	return *this;
}

Hash& Hash::update(const std::string_view& data) {
	this->update(data.data(), data.size());
	return *this;
}

void Hash::reset() {
	if (EVP_DigestInit_ex(ctx_, md_, nullptr) != 1)
		throw std::runtime_error("Digest: EVP_DigestInit_ex (reset) failed");

	this->finalized_ = false;
	this->cached_result_.clear();
}

void Hash::operator<<(std::istream& stream) {
	char buffer[HASHER_BUFFER_SIZE];
	while (stream.read(buffer, HASHER_BUFFER_SIZE) || stream.gcount() > 0) {
		this->update(buffer, stream.gcount());
	}
}

std::string Hash::final() {
	// idempotent
	if(!this->finalized_) {
		this->finalized_ = true;

		int md_size = EVP_MD_size(md_);
		if (md_size <= 0) throw std::runtime_error("Digest: EVP_MD_size <= 0");

		uint32_t out_len = 0;
		this->cached_result_.resize(md_size);
		if (EVP_DigestFinal_ex(ctx_, reinterpret_cast<unsigned char*>(this->cached_result_.data()), &out_len) != 1)
			throw std::runtime_error("Digest: EVP_DigestFinal_ex failed");

		this->cached_result_.resize(out_len);
	}
	return this->cached_result_;
}

std::string Hash::hexdigest(const std::string& bin_hash) {
	static const char hex_chars[] = "0123456789abcdef";
	std::string hex_hash;
	hex_hash.reserve(bin_hash.size() * 2);
	for (unsigned char c : bin_hash) {
		hex_hash += hex_chars[(c >> 4) & 0x0F];
		hex_hash += hex_chars[c & 0x0F];
	}
	return hex_hash;
}

}
