#pragma once

#include <cstddef>
#include <cstdint>

// XC2 passes this value directly to NVN's texture builder. 0x25 is the
// RGBA8_UNORM format used by the game's own main-frame-buffer allocation.
enum class GrlSurfaceFormat : std::uint32_t {
	RGBA8Unorm = 0x25
};

namespace grlib {

	class CGLibTextureBuffer {
	   public:
		CGLibTextureBuffer();
		~CGLibTextureBuffer();

		bool isInitialized() const;

	   private:
		std::byte storage[0x70];
	};

	static_assert(sizeof(CGLibTextureBuffer) == 0x70);

	class CGLibAcc2DTexture {
	   public:
		CGLibAcc2DTexture();
		~CGLibAcc2DTexture();

		bool lock(CGLibTextureBuffer& texture);
		void unlock();
		void setFloat(
			float red,
			float green,
			float blue,
			float alpha,
			unsigned int x,
			unsigned int y
		);

	   private:
		std::byte storage[0xB0];
	};

	static_assert(sizeof(CGLibAcc2DTexture) == 0xB0);

	class CGLibTexture {
	   public:
		static void createTextureBuff(
			CGLibTextureBuffer& texture,
			unsigned int width,
			unsigned int height,
			GrlSurfaceFormat format
		);
	};

} // namespace grlib
