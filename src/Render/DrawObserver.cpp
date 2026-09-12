#include "Render/DrawObserver.h"

namespace Render
{
	bool TechniqueFilter::Matches(const CurrentTechnique& a_current) const noexcept
	{
		return a_current.valid &&
		       a_current.classIndex == classIndex &&
		       (technique == kAnyTechnique || a_current.technique == technique);
	}
}
