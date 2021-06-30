/**********************************************************************

   Audacity: A Digital Audio Editor
   Audacity(R) is copyright (c) 1999-2021 Audacity Team.
   License: GPL v2.  See License.txt.

   DeepModelManager.cpp
   Hugo Flores Garcia

******************************************************************/

#include "DeepModelManager.h"

#include "NetworkManager.h"
#include "IResponse.h"
#include "Request.h"

DeepModelManager::DeepModelManager()
{}

DeepModelManager::~DeepModelManager()
{}

bool DeepModelManager::RegisterModel(std::string path)
{
   std::unique_ptr<DeepModel> model = std::make_unique<DeepModel>();

   if (!model->Load(path))
      return false;

   rapidjson::Document metadata = model->GetMetadata();

   assert(metadata.HasMember("name"));
   assert(metadata["name"].IsString());
   std::string name = metadata["name"].GetString();

   assert(metadata.HasMember("effect"));
   assert(metadata["effect"].IsString());

   // TODO: need to add real validation for the model

   mModels[name] = model;

}


std::unique_ptr<DeepModel> DeepModelManager::FromHuggingFace(std::string url)
{
   std::unique_ptr<DeepModel> model = std::make_unique<DeepModel>();

   audacity::network_manager::Request request(url);
   auto response = audacity::network_manager::NetworkManager::GetInstance().doGet(request);


   return model;
}