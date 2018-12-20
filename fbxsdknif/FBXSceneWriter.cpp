#include <fbxsdk/scene/fbxscene.h>
#include <fbxsdk/scene/geometry/fbxnode.h>
#include <fbxsdk/scene/geometry/fbxmesh.h>
#include <fbxsdk/scene/geometry/fbxskeleton.h>
#include <fbxsdk/scene/geometry/fbxskin.h>
#include <fbxsdk/scene/geometry/fbxcluster.h>

#include <nifparse/NIFFile.h>

#include "FBXSceneWriter.h"
#include "NIFUtils.h"
#include "SkeletonProcessor.h"

namespace fbxnif {
	FBXSceneWriter::FBXSceneWriter(const NIFFile &file, const SkeletonProcessor &skeleton) : m_file(file), m_skeleton(skeleton) {

	}

	FBXSceneWriter::~FBXSceneWriter() {

	}

	void FBXSceneWriter::write(FbxDocument *document) {
		m_scene = FbxCast<FbxScene>(document);

		if (m_file.rootObjects().data.empty()) {
			throw std::runtime_error("no root object in NIF");
		}
			   
		FbxAxisSystem(FbxAxisSystem::eZAxis, FbxAxisSystem::eParityOdd, FbxAxisSystem::eRightHanded).ConvertScene(m_scene);
		FbxSystemUnit(1.4287109375).ConvertScene(m_scene);

		convertSceneNode(std::get<NIFReference>(m_file.rootObjects().data.front()), m_scene->GetRootNode());
	}

	void FBXSceneWriter::convertNiNode(const NIFDictionary &dict, fbxsdk::FbxNode *node) {
		if (dict.typeChain.front() != Symbol("NiNode")) {
			fprintf(stderr, "FBXSceneWriter: %s: unsupported NiNode subclass interpreted as NiNode: %s\n", node->GetName(), dict.typeChain.front().toString());
		}
		
		for (const auto &child : dict.getValue<NIFArray>("Children").data) {
			auto childRef = std::get<NIFReference>(child);
			if (!childRef.ptr)
				continue;

			convertSceneNode(childRef, node);
		}
	}

	void FBXSceneWriter::convertSceneNode(const NIFReference &var, fbxsdk::FbxNode *containingNode) {
		const auto &dict = std::get<NIFDictionary>(*var.ptr);
		if (!dict.kindOf("NiAVObject")) {
			throw std::runtime_error("scene node is not an instance of NiAVObject");
		}

		bool forceHidden = dict.isA("RootCollisionNode");

		auto node = FbxNode::Create(m_scene, getString(dict.getValue<NIFDictionary>("Name"), m_file.header()).c_str());
		containingNode->AddChild(node);

		m_nodeMap.emplace(var.ptr, node);

		fprintf(stderr, "%s: %s\n", node->GetName(), dict.typeChain.front().toString());

		node->Visibility = (dict.getValue<uint32_t>("Flags") & NiAVObjectFlagHidden) == 0 && !forceHidden;
		node->LclTranslation = getVector3(dict.getValue<NIFDictionary>("Translation"));
		node->LclRotation = getMatrix3x3(dict.getValue<NIFDictionary>("Rotation")).GetR();
		node->LclScaling = FbxDouble3(dict.getValue<float>("Scale"));

		if (m_skeleton.allBones().count(var.ptr) != 0) {
			auto skeleton = FbxSkeleton::Create(m_scene, (std::string(node->GetName()) + " Skeleton").c_str());
			node->AddNodeAttribute(skeleton);

			if (m_skeleton.commonBoneRoot() == var.ptr) {
				skeleton->SetSkeletonType(FbxSkeleton::eRoot);
			}
			else {
				skeleton->SetSkeletonType(FbxSkeleton::eLimbNode);
			}
		}

		if (dict.kindOf("NiNode")) {
			convertNiNode(dict, node);
		}
		else if (dict.kindOf("NiTriBasedGeom")) {
			convertNiTriBasedGeom(dict, node);
		}
		else {
			fprintf(stderr, "FBXSceneWriter: %s: unsupported type: %s\n", node->GetName(), dict.typeChain.front().toString());
		}
	}
	
	template<typename ElementType>
	void FBXSceneWriter::importVectorElement(const NIFDictionary &data, FbxMesh *mesh, const Symbol &name, ElementType *(FbxGeometryBase::*createElement)()) {
		if (data.data.count(name) != 0) {
			const auto &vectors = data.getValue<NIFArray>(name);

			auto vectorElement = (mesh->*createElement)();
			vectorElement->SetMappingMode(FbxGeometryElement::eByControlPoint);
			vectorElement->SetReferenceMode(FbxGeometryElement::eDirect);

			auto &vectorData = vectorElement->GetDirectArray();
			vectorData.Resize(static_cast<int>(vectors.data.size()));

			for (size_t index = 0, size = vectors.data.size(); index < size; index++) {
				vectorData.SetAt(static_cast<int>(index), getVector3(std::get<NIFDictionary>(vectors.data[index])));
			}
		}
	}

	void FBXSceneWriter::convertNiTriBasedGeom(const NIFDictionary &dict, fbxsdk::FbxNode *node) {
		Symbol symVertices("Vertices");
		Symbol symVertexColors("Vertex Colors");

		auto mesh = FbxMesh::Create(m_scene, (std::string(node->GetName()) + " Mesh").c_str());
		node->AddNodeAttribute(mesh);
		
		const auto &data = std::get<NIFDictionary>(*dict.getValue<NIFReference>("Data").ptr);

		/*
		 * NiGeometry data
		 */

		if (data.data.count(symVertices) != 0) {
			const auto &vertices = data.getValue<NIFArray>(symVertices);

			mesh->InitControlPoints(static_cast<int>(vertices.data.size()));
			auto controlPoints = mesh->GetControlPoints();
			for (size_t index = 0, size = vertices.data.size(); index < size; index++) {
				controlPoints[index] = getVector3(std::get<NIFDictionary>(vertices.data[index]));
			}
		}

		importVectorElement(data, mesh, "Normals", &FbxMesh::CreateElementNormal);
		importVectorElement(data, mesh, "Tangents", &FbxMesh::CreateElementTangent);
		importVectorElement(data, mesh, "Bitangents", &FbxMesh::CreateElementBinormal);

		if (data.data.count(symVertexColors) != 0) {
			const auto &vertexColors = data.getValue<NIFArray>(symVertexColors);

			auto colorElement = mesh->CreateElementVertexColor();
			colorElement->SetMappingMode(FbxGeometryElement::eByControlPoint);
			colorElement->SetReferenceMode(FbxGeometryElement::eDirect);

			auto &colorData = colorElement->GetDirectArray();
			colorData.Resize(static_cast<int>(vertexColors.data.size()));
			for (size_t index = 0, size = vertexColors.data.size(); index < size; index++) {
				colorData.SetAt(static_cast<int>(index), getColor4(std::get<NIFDictionary>(vertexColors.data[index])));
			}
		}

		const auto &uvSets = data.getValue<NIFArray>("UV Sets");
		for (size_t uvSetIndex = 0, uvSetCount = uvSets.data.size(); uvSetIndex < uvSetCount; uvSetIndex++) {
			const auto &uvSet = std::get<NIFArray>(uvSets.data[uvSetIndex]);

			std::stringstream uvName;
			uvName << "UV" << uvSetIndex;
			
			auto uv = mesh->CreateElementUV(uvName.str().c_str(), FbxLayerElement::eTextureDiffuse);
			uv->SetMappingMode(FbxGeometryElement::eByControlPoint);
			uv->SetReferenceMode(FbxGeometryElement::eDirect);

			auto &uvData = uv->GetDirectArray();
			uvData.Resize(static_cast<int>(uvSet.data.size()));
			for (size_t index = 0, size = uvSet.data.size(); index < size; index++) {
				uvData.SetAt(static_cast<int>(index), getTexCoord(std::get<NIFDictionary>(uvSet.data[static_cast<int>(index)])));
			}
		}

		/*
		 * Type-specific data
		 */

		if (data.isA("NiTriShapeData")) {
			Symbol symTriangles("Triangles");
			Symbol symV1("v1");
			Symbol symV2("v2");
			Symbol symV3("v3");

			if (data.data.count(symTriangles) != 0) {
				const auto &triangles = data.getValue<NIFArray>(symTriangles);

				mesh->ReservePolygonCount(static_cast<int>(triangles.data.size()));
				mesh->ReservePolygonVertexCount(static_cast<int>(3 * triangles.data.size()));

				for (const auto &triangleValue : triangles.data) {
					const auto &triangle = std::get<NIFDictionary>(triangleValue);

					mesh->BeginPolygon(-1, -1, -1, false);

					mesh->AddPolygon(triangle.getValue<uint32_t>(symV1));
					mesh->AddPolygon(triangle.getValue<uint32_t>(symV2));
					mesh->AddPolygon(triangle.getValue<uint32_t>(symV3));

					mesh->EndPolygon();
				}
			}
		}
		else {
			fprintf(stderr, "%s: unknown type of geometry data: %s\n",
				mesh->GetName(), data.typeChain.front().toString());
		}

		/*
		 * Skinning
		 */

		Symbol symSkinInstance("Skin Instance");
		if (dict.data.count(symSkinInstance) != 0) {
			const auto &skinPtr = dict.getValue<NIFReference>(symSkinInstance).ptr;
			if (skinPtr) {
				const auto &skinInstance = std::get<NIFDictionary>(*skinPtr);
				const auto &skinData = std::get<NIFDictionary>(*skinInstance.getValue<NIFReference>("Data").ptr);

				printf("Bone list index: %zu\n", skinData.data.find("Bone List")->second.index());

				auto skin = FbxSkin::Create(m_scene, (std::string(mesh->GetName()) + " Skin").c_str());

				const auto &skinDataBones = skinData.getValue<NIFArray>("Bone List").data;

				// SKIN TRANSFORM?

				size_t boneIndex = 0;
				for (const auto &bone : skinInstance.getValue<NIFArray>("Bones").data) {
					std::shared_ptr<NIFVariant> bonePtr(std::get<NIFPointer>(bone).ptr);

					auto cluster = FbxCluster::Create(m_scene, "");

					auto it = m_nodeMap.find(bonePtr);
					if (it == m_nodeMap.end()) {
						throw std::logic_error("bone is not in the node map");
					}

					cluster->SetLink(it->second);
					cluster->SetLinkMode(FbxCluster::eTotalOne);

					const auto &boneData = std::get<NIFDictionary>(skinDataBones[boneIndex]);
					// SKIN TRANSFORM?

					cluster->SetTransformMatrix(
						getTransform(boneData.getValue<NIFDictionary>("Skin Transform"))
					);

					for (const auto &weight : boneData.getValue<NIFArray>("Vertex Weights").data) {
						const auto &weightDict = std::get<NIFDictionary>(weight);

						cluster->AddControlPointIndex(
							weightDict.getValue<uint32_t>("Index"),
							weightDict.getValue<float>("Weight")
						);
					}

					skin->AddCluster(cluster);

					boneIndex++;
				}

				mesh->AddDeformer(skin);
			}
		}
	}
}
