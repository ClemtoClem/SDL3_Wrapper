#pragma once

#include "../core/core.hpp"
#include "../math/math.hpp"

namespace generators {

	template <typename Array>
	class Generator {
	public:
		virtual ~Generator() = default;
		
		// Utilisation de std::expected (C++23) pour retourner la grille ou une erreur string
		virtual Result<Array, String> Generate() = 0;
	};

} /* namespace generators */