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

DeepModelManager::DeepModelManager() : mSchema(ModelCard::CreateFromFile(kSchemaPath)),
                                       mCards(ModelCardCollection(mSchema))
{

}

DeepModelManager::~DeepModelManager()
{

}

DeepModelManager& DeepModelManager::Get()
{
   static DeepModelManager manager;

   // NOTE: DEBUG
   manager.FetchCards();

   for (auto card: manager.mCards)
   {  
      rapidjson::StringBuffer sBuffer;
      rapidjson::Writer<rapidjson::StringBuffer> writer(sBuffer);

      (*card.GetDoc()).Accept(writer);
      std::string output = sBuffer.GetString();

      std::cout<<output<<std::endl;
   }
   // NOTE: DEBUG

   return manager;
}

FilePath DeepModelManager::DLModelsDir()
{
   wxFileName modelsDir(FileNames::BaseDir(), wxEmptyString);
   modelsDir.AppendDir(wxT("deeplearning-models"));
   return modelsDir.GetFullPath();
}

FilePath DeepModelManager::GetRepoDir(const ModelCard &card)
{
   // TODO: do we really want these fields in the JSON file?
   // or should they be members of the ModelCard class? 
   wxFileName repoDir = DLModelsDir();
   wxASSERT(card.GetDoc()->HasMember("repo_user"));
   wxASSERT(card.GetDoc()->HasMember("repo_name"));
   repoDir.AppendDir(card["repo_user"].GetString());
   repoDir.AppendDir(card["repo_name"].GetString());
   return repoDir.GetFullPath();
}

std::unique_ptr<DeepModel> DeepModelManager::GetModel(ModelCard &card)
{
   std::unique_ptr<DeepModel> model = std::make_unique<DeepModel>();
   model->SetCard(card);

   // TODO: raise exception if 
   // TODO: only do if model is loaded, else return  an empty model
   // GetRepoDir won't work if the card is empty
   wxFileName path = wxFileName(GetRepoDir(card), "model.pt");

   // finally, load
   model->Load(path.GetFullPath().ToStdString());

   return model;
}  

ModelCard DeepModelManager::GetCached(std::string &effectID)
{
   // TODO
   return ModelCard();
}

void DeepModelManager::FetchCards(ProgressDialog *progress)
{
   if (progress)
      progress->SetMessage(XO("Fetching Model Repos..."));

   RepoIDList repos = mHFWrapper.FetchRepos();

   //TODO: exception handling 
   int total = repos.size();
   for (int idx = 0;  idx < repos.size() ; idx++)
   {
      std::string repoId = repos[idx];

      // fetch card and insert into mCards if not already there. 
      ModelCard card = mHFWrapper.GetCard(repoId);

      if (progress)
            progress->Update(idx, total, 
                           (XO("Fetching Model &%s").Format(repoId)));

      try
      {
         mCards.Insert(card);
      }
      catch (const std::exception& e)
      {
         // TODO: do we even let the user know that
         // the card didn't validate?  
         wxLogDebug(wxString("Failed to validate metadata.json for repo %s")
                                                            .Format(wxString(repoId)));
         wxLogDebug(e.what());
      }
   }

   // TODO: load from the installed repos as well
   // make sure we don't have the same repo twice
   // what if we got it from the HF wrapper but we 
   // already had it installed? 
}

// HuggingFaceWrapper Implementation

std::string HuggingFaceWrapper::GetRootURL(const std::string &repoID)
{
   std::stringstream url;
   url<<"https://huggingface.co/"<<repoID<<"/resolve/main/";
   return url.str();
}

HuggingFaceWrapper::HuggingFaceWrapper()
{
   mAPIEndpoint = "https://huggingface.co/api/";
}

RepoIDList HuggingFaceWrapper::FetchRepos()
{
   RepoIDList repos;

   std::string query = mAPIEndpoint + "models?filter=audacity";

   // TODO: handle exception in main thread
   CompletionHandler handler = [&repos](int httpCode, std::string body)
   {
      // TODO: http code handling
      ModelCard reposCard = ModelCard(body);
      std::shared_ptr<const rapidjson::Document> reposDoc = reposCard.GetDoc();

      for (rapidjson::Value::ConstValueIterator itr = reposDoc->Begin();
                                                itr != reposDoc->End();
                                                ++itr)
         // TODO: need to raise an exception if we can't get the object 
         // or modelId
         repos.emplace_back(itr->GetObject()["modelId"].GetString());
   };

   doGet(query, handler, true);

   return repos;
}

ModelCard HuggingFaceWrapper::GetCard(const std::string &repoID)
{ 
   std::string modelCardUrl = GetRootURL(repoID) + "metadata.json";
   ModelCard card = ModelCard();
   // TODO: how do you handle an exception inside a thread, like this one? 
   CompletionHandler completionHandler = [&card](int httpCode, std::string body)
   { 
      if (!(httpCode == 200))
      {
         std::stringstream msg;
         msg << "GET request failed. Error code: " << httpCode;
         throw ModelManagerException(msg.str());
      }
      else
      { 
         card = ModelCard(body);
      }
   };

   doGet(modelCardUrl, completionHandler, true);
   
   return card;
}

void HuggingFaceWrapper::DownloadModel(const ModelCard &card, const std::string &path)
{
   std::string modelUrl = GetRootURL(card["repo_id"].GetString()) + "/model.pt";

   CompletionHandler completionHandler = [&card, &path](int httpCode, std::string body)
   {
      if (!(httpCode == 200))
      {
         std::stringstream msg;
         msg << "GET request failed. Error code: " << httpCode;
         throw ModelManagerException(msg.str());
      }
      else
      {  
         std::istringstream modulestr(body);

         DeepModel tmpModel = DeepModel();
         ModelCard cardCopy = ModelCard(card);
         tmpModel.SetCard(cardCopy);
         tmpModel.Load(modulestr);
         tmpModel.Save(path);
      }
   };
}

void HuggingFaceWrapper::doGet(std::string url, CompletionHandler completionHandler, bool block /*, ProgressCallback progress */)
{
   using namespace audacity::network_manager;

   Request request(url);

   if (block)
      request.setBlocking(true);

   NetworkManager& manager = NetworkManager::GetInstance();
   ResponsePtr response = manager.doGet(request);

#if 0
   // set callback for download progress
   response->setDownloadProgressCallback(progress);
#endif

   response->setRequestFinishedCallback(
      [response, handler = std::move(completionHandler)](IResponse*) {
         const std::string responseData = response->readAll<std::string>();

         wxLogDebug(responseData.c_str());
         if (handler)
            handler(response->getHTTPCode(), responseData);
      });

   std::cout<<"success"<<std::endl;
   return;
}