#ifndef FBX_SCENE_WRITER_H
#define FBX_SCENE_WRITER_H

#include "FBXNIFPluginNS.h"

#include <nifparse/Types.h>

#include <fbxsdk/core/math/fbxvector4.h>
#include <fbxsdk/core/math/fbxaffinematrix.h>
#include <fbxsdk/scene/geometry/fbxmesh.h>

#include <unordered_map>

namespace nifparse {
	class NIFFile;
}

namespace fbxsdk {
	class FbxDocument;
	class FbxNode;
	class FbxScene;
}

namespace fbxnif {
	class SkeletonProcessor;

	enum : uint32_t {
		// NiAVObject flags
		NiAVObjectFlagHidden = 1 << 0
	};

	class FBXSceneWriter {
	public:
		FBXSceneWriter(const NIFFile &file, const SkeletonProcessor &skeleton);
		~FBXSceneWriter();

		FBXSceneWriter(const FBXSceneWriter &other) = delete;
		FBXSceneWriter &operator =(const FBXSceneWriter &other) = delete;

		void write(FbxDocument *document);

	private:
		void convertSceneNode(const NIFReference &var, fbxsdk::FbxNode *containingNode);

		void convertNiNode(const NIFDictionary &dict, fbxsdk::FbxNode *node);
		void convertNiTriBasedGeom(const NIFDictionary &dict, fbxsdk::FbxNode *node);
		

		template<typename ElementType>
		void importVectorElement(const NIFDictionary &data, FbxMesh *mesh, const Symbol &name, ElementType *(FbxGeometryBase::*createElement)());

		const NIFFile &m_file;
		const SkeletonProcessor &m_skeleton;
		fbxsdk::FbxScene *m_scene;
		std::unordered_map<std::shared_ptr<NIFVariant>, FbxNode *> m_nodeMap;
	};
}

#endif
