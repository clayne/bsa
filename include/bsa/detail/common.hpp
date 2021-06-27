#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <span>
#include <string>
#include <type_traits>

#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/predef.h>

#include "bsa/fwd.hpp"

#define BSA_MAKE_ENUM_OPERATOR_PAIR(a_type, a_op)                                     \
	[[nodiscard]] constexpr a_type operator a_op(a_type a_lhs, a_type a_rhs) noexcept \
	{                                                                                 \
		return static_cast<a_type>(                                                   \
			static_cast<std::underlying_type_t<a_type>>(a_lhs)                        \
				a_op static_cast<std::underlying_type_t<a_type>>(a_rhs));             \
	}                                                                                 \
                                                                                      \
	constexpr a_type& operator a_op##=(a_type& a_lhs, a_type a_rhs) noexcept          \
	{                                                                                 \
		return a_lhs = a_lhs a_op a_rhs;                                              \
	}

#define BSA_MAKE_ALL_ENUM_OPERATORS(a_type)                                  \
	static_assert(std::is_enum_v<a_type>, "\'" #a_type "\' is not an enum"); \
                                                                             \
	[[nodiscard]] constexpr a_type operator~(a_type a_val) noexcept          \
	{                                                                        \
		return static_cast<a_type>(                                          \
			~static_cast<std::underlying_type_t<a_type>>(a_val));            \
	}                                                                        \
                                                                             \
	BSA_MAKE_ENUM_OPERATOR_PAIR(a_type, ^)                                   \
	BSA_MAKE_ENUM_OPERATOR_PAIR(a_type, &)                                   \
	BSA_MAKE_ENUM_OPERATOR_PAIR(a_type, |)                                   \
	BSA_MAKE_ENUM_OPERATOR_PAIR(a_type, <<)                                  \
	BSA_MAKE_ENUM_OPERATOR_PAIR(a_type, >>)

namespace bsa
{
	using namespace std::literals;
}

namespace bsa::detail
{
	namespace concepts
	{
		template <class T>
		concept integral =
			std::same_as<T, std::byte> ||
			std::same_as<T, std::int8_t> ||
			std::same_as<T, std::uint8_t> ||
			std::same_as<T, std::int16_t> ||
			std::same_as<T, std::uint16_t> ||
			std::same_as<T, std::int32_t> ||
			std::same_as<T, std::uint32_t> ||
			std::same_as<T, std::int64_t> ||
			std::same_as<T, std::uint64_t>;

		template <class T>
		concept stringable =
			std::constructible_from<std::string, T>;
	}

	[[noreturn]] inline void declare_unreachable()
	{
		assert(false);
#if BOOST_COMP_GNUC || BOOST_COMP_CLANG
		__builtin_unreachable();
#elif BOOST_COMP_MSVC || BOOST_COMP_EDG
		__assume(false);
#else
		static_assert(false);
#endif
	}

	template <class Enum>
	[[nodiscard]] constexpr auto to_underlying(Enum a_val) noexcept
		-> std::underlying_type_t<Enum>
	{
		static_assert(std::is_enum_v<Enum>, "Input type is not an enumeration");
		return static_cast<std::underlying_type_t<Enum>>(a_val);
	}

	template <class T, bool RECURSE, class Hash>
	class index_t final
	{
	public:
		using value_type = T;
		using pointer = T*;
		using reference = T&;

		index_t() noexcept = default;

		[[nodiscard]] auto operator[](Hash a_hash) const noexcept  //
			requires(RECURSE)
		{
			return (**this)[a_hash];
		}

		template <concepts::stringable String>
		[[nodiscard]] auto operator[](String&& a_path) const noexcept  //
			requires(RECURSE)
		{
			return (**this)[std::forward<String>(a_path)];
		}

		[[nodiscard]] explicit operator bool() const noexcept { return _proxy != nullptr; }

		[[nodiscard]] auto operator*() const noexcept -> reference
		{
			assert(*this);
			return *_proxy;
		}

		[[nodiscard]] auto operator->() const noexcept -> pointer { return _proxy; }

	protected:
		friend class tes3::archive;
		friend class tes4::archive;
		friend class tes4::directory;

		explicit index_t(reference a_value) noexcept :
			_proxy(&a_value)
		{}

	private:
		pointer _proxy{ nullptr };
	};

	class istream_t final
	{
	public:
		using stream_type = boost::iostreams::mapped_file_source;

		istream_t(std::filesystem::path a_path) noexcept;
		istream_t(const istream_t&) = delete;
		istream_t(istream_t&&) = delete;
		~istream_t() noexcept = default;
		istream_t& operator=(const istream_t&) = delete;
		istream_t& operator=(istream_t&&) = delete;

		[[nodiscard]] bool is_open() const noexcept { return _file.is_open(); }

		template <concepts::integral T>
		[[nodiscard]] T read(std::endian a_endian = std::endian::little) noexcept
		{
			using integral_t = typename std::conditional_t<
				std::is_enum_v<T>,
				std::underlying_type<T>,
				std::type_identity<T>>::type;
			auto result = integral_t{ 0 };
			const auto bytes = read_bytes(sizeof(T));

			switch (a_endian) {
			case std::endian::little:
				for (std::size_t i = 0; i < bytes.size(); ++i) {
					result |= static_cast<T>(bytes[i]) << i * 8u;
				}
				break;
			case std::endian::big:
				for (std::ptrdiff_t i = std::ssize(bytes) - 1; i >= 0; --i) {
					result |=
						static_cast<integral_t>(bytes[static_cast<std::size_t>(i)])
						<< static_cast<std::size_t>(i) * 8u;
				}
				break;
			default:
				declare_unreachable();
			}

			return static_cast<T>(result);
		}

		[[nodiscard]] auto read_bytes(std::size_t a_bytes) noexcept
			-> std::span<const std::byte>;

		void seek_absolute(std::size_t a_pos) noexcept { _pos = a_pos; }
		void seek_relative(std::ptrdiff_t a_off) noexcept { _pos += a_off; }

		[[nodiscard]] auto tell() const noexcept { return _pos; }
		[[nodiscard]] auto rdbuf() const noexcept -> const stream_type& { return _file; }

		template <concepts::integral T>
		friend istream_t& operator>>(istream_t& a_in, T& a_value) noexcept
		{
			a_value = a_in.read<T>();
			return a_in;
		}

		template <std::size_t N>
		friend istream_t& operator>>(
			istream_t& a_in,
			std::array<std::byte, N>& a_value) noexcept
		{
			const auto bytes = a_in.read_bytes(a_value.size());
			std::copy(bytes.begin(), bytes.end(), a_value.begin());
			return a_in;
		}

	private:
		stream_type _file;
		std::size_t _pos{ 0 };
	};

	template <class T>
	struct istream_proxy final
	{
		T d;
		boost::iostreams::mapped_file_source f;
	};

	template <class Key, class Hash>
	struct key_compare_t
	{
		using is_transparent = int;

		[[nodiscard]] auto operator()(
			const Key& a_lhs,
			const Key& a_rhs) const noexcept
			-> bool { return a_lhs.hash() < a_rhs.hash(); }

		[[nodiscard]] auto operator()(
			const Key& a_lhs,
			const Hash& a_rhs) const noexcept
			-> bool { return a_lhs.hash() < a_rhs; }

		[[nodiscard]] auto operator()(
			const Hash& a_lhs,
			const Key& a_rhs) const noexcept
			-> bool { return a_lhs < a_rhs.hash(); }
	};

	class ostream_t final
	{
	public:
		ostream_t(std::filesystem::path a_path) noexcept;
		ostream_t(const ostream_t&) = delete;
		ostream_t(ostream_t&&) = delete;
		~ostream_t() noexcept;
		ostream_t& operator=(const ostream_t&) = delete;
		ostream_t& operator=(ostream_t&&) = delete;

		[[nodiscard]] bool is_open() const noexcept { return _file != nullptr; }

		template <concepts::integral T>
		void write(T a_value, std::endian a_endian = std::endian::little) noexcept
		{
			using integral_t = typename std::conditional_t<
				std::is_enum_v<T>,
				std::underlying_type<T>,
				std::type_identity<T>>::type;
			const auto value = static_cast<integral_t>(a_value);
			std::array<std::byte, sizeof(T)> bytes;

			switch (a_endian) {
			case std::endian::little:
				for (std::size_t i = 0; i < bytes.size(); ++i) {
					bytes[i] = static_cast<std::byte>((value >> i * 8u) & 0xFFu);
				}
				break;
			case std::endian::big:
				for (std::ptrdiff_t i = std::ssize(bytes) - 1; i >= 0; --i) {
					bytes[static_cast<std::size_t>(i)] =
						static_cast<std::byte>(
							(value >> static_cast<std::size_t>(i) * 8u) &
							0xFFu);
				}
				break;
			default:
				declare_unreachable();
			}

			write_bytes(std::span<const std::byte>{ bytes.data(), bytes.size() });
		}

		void write_bytes(std::span<const std::byte> a_bytes) noexcept
		{
			std::fwrite(a_bytes.data(), 1, a_bytes.size_bytes(), _file);
		}

		template <concepts::integral T>
		friend ostream_t& operator<<(ostream_t& a_out, T a_value) noexcept
		{
			a_out.write(a_value);
			return a_out;
		}

		template <std::size_t N>
		friend ostream_t& operator<<(
			ostream_t& a_out,
			const std::array<std::byte, N>& a_value) noexcept
		{
			a_out.write_bytes({ a_value.data(), a_value.size() });
			return a_out;
		}

	private:
		std::FILE* _file{ nullptr };
	};

	class restore_point final
	{
	public:
		restore_point(const restore_point&) = delete;

		explicit restore_point(istream_t& a_proxy) noexcept :
			_proxy(a_proxy),
			_pos(a_proxy.tell())
		{}

		~restore_point() noexcept { _proxy.seek_absolute(_pos); }

	private:
		istream_t& _proxy;
		std::size_t _pos;
	};
}
