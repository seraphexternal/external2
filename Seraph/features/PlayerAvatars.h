#pragma once

#include <cstdint>
#include <d3d11.h>
#include <string>

namespace Cheat {
namespace Features {
namespace PlayerAvatars {

	void Tick();
	void Clear();

	ID3D11ShaderResourceView* Get(std::int64_t user_id);

	// из кэша name→uid (0 если ещё не резолвнули)
	std::int64_t LookupUserId(const std::string& username);

}
}
}