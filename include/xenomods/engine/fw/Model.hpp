#pragma once

namespace ml {
	class ScnObjModel;
}

namespace fw {

	class ModelObject {
	   public:
		ml::ScnObjModel* getInterface();
	};

} // namespace fw
