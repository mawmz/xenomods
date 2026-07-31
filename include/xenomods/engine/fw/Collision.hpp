#pragma once

#include <cstdint>

namespace fw {

	enum class ColiState : std::uint32_t {
		Ground = 0,
		Character = 1,
	};

	class ColiObject {
	   public:
		bool testState(ColiState state) const;
		bool isContactWall() const;
	};

} // namespace fw
