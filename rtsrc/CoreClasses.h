//
// Created by znix on 21/07/22.
//

#pragma once

#include "Obj.h"
#include "ObjClass.h"

#include <memory>

/// Singleton containing pointers to all the core classes (the classes that are treated as intrinsics).
class CoreClasses {
  public:
	static CoreClasses *Instance();

	CoreClasses(const CoreClasses &) = delete;
	CoreClasses &operator=(const CoreClasses &) = delete;

	/// The object everything extends from
	inline ObjClass &Object() { return m_object; }

	/// The class every other class eventually uses as it's superclass
	inline ObjClass &RootClass() { return m_rootClass; }

	ObjSystem &System();

  private:
	CoreClasses();
	~CoreClasses();

	ObjClass m_object;
	ObjClass m_objectMeta;
	ObjClass m_rootClass;

	// All other classes must be lazy-initialised to avoid them calling Instance while it's first running
	std::unique_ptr<ObjSystem> m_system;
};
