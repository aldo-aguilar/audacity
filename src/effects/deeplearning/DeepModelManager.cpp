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

#include <wx/tokenzr.h>

using namespace audacity;

// TODO: the manager is not thread safe? 
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
   
   wxASSERT(card.GetDoc()->HasMember("author"));
   wxASSERT(card.GetDoc()->HasMember("name"));

   repoDir.AppendDir(card["author"].GetString());
   if (!repoDir.Exists())
      repoDir.Mkdir();

   repoDir.AppendDir(card["name"].GetString());
   if (!repoDir.Exists())
      repoDir.Mkdir();

   return repoDir.GetFullPath();
}

std::unique_ptr<DeepModel> DeepModelManager::GetModel(ModelCard &card)
{
   if (!IsInstalled(card))
   {
      wxASSERT(IsInstalled(card)); 
      throw ModelManagerException("model is not loaded.");
   }

   std::unique_ptr<DeepModel> model = std::make_unique<DeepModel>();
   model->SetCard(card);

   // GetRepoDir won't work if the card is empty
   wxFileName path = wxFileName(GetRepoDir(card), "model.pt");

   // finally, load
   model->Load(path.GetFullPath().ToStdString());

   return model;
}  
void DeepModelManager::FetchCards(ProgressDialog *progress)
{
   FetchInstalledCards();

   if (progress)
      progress->SetMessage(XO("Fetching Model Repos..."));

   RepoIDList repos = mHFWrapper.FetchRepos();

   int total = repos.size();
   for (int idx = 0;  idx < repos.size() ; idx++)
   {
      std::string repoId = repos[idx];

      if (progress)
            progress->Update(idx, total, 
                           (XO("Fetching &%s...").Format(repoId)));

      // grab the card 
      ModelCard card;
      try
      {
         card = mHFWrapper.GetCard(repoId);
      }
      catch (const ModelException &e)
      {
         std::stringstream msg;
         msg<<"failed to parse metadata entry: "<<std::endl;
         msg<<e.what()<<std::endl;
         wxLogDebug(wxString(msg.str()));

         continue;
      }

      // validate it and insert it into collection
      try
      {
         mCards.Insert(card);
      }
      catch (const std::exception& e)
      {
         std::stringstream msg;
         msg<<"Failed to validate metadata.json for repo "<<repoId<<std::endl;
         msg<<e.what()<<std::endl;
         wxLogDebug(wxString(msg.str()));
      }
   }
}

void DeepModelManager::FetchInstalledCards(ProgressDialog *progress)
{
   wxString glob = wxFileName(DLModelsDir(), "**").GetFullPath();
   wxString pattern = wxFileName(glob, "*.json").GetFullPath();
   wxString cardF = wxFindFirstFile(pattern);
   size_t numCardsRead = 0;

   if (progress)
      progress->SetMessage(XO("Fetching local models..."));

   while (!cardF.empty())
   {

      // grab the card 
      ModelCard card;
      try
      {
         // TODO: this still throws a rapidjson assert if its not a valid json string i think
         card = ModelCard::CreateFromFile(cardF.ToStdString());
      }
      catch (const ModelException &e)
      {
         std::stringstream msg;
         msg<<"failed to parse metadata entry: "<<std::endl;
         msg<<e.what()<<std::endl;
         wxLogDebug(wxString(msg.str()));

         continue;
      }

      std::string repoId = card.GetRepoID();

      // validate it and insert it into collection
      try
      {
         mCards.Insert(card);
      }
      catch (const std::exception& e)
      {
         std::stringstream msg;
         msg<<"Failed to validate metadata.json for repo "<<repoId<<std::endl;
         msg<<e.what()<<std::endl;
         wxLogDebug(wxString(msg.str()));
      }
      
      if (progress)
      progress->Update(numCardsRead, 0, 
                     (XO("Fetching &%s...").Format(repoId)));

      numCardsRead++;
      cardF = wxFindNextFile();
   }
}

bool DeepModelManager::IsInstalled(ModelCard &card)
{
   FilePath repoDir = GetRepoDir(card);

   wxFileName modelPath = wxFileName(repoDir, "model.pt");
   wxFileName metadataPath = wxFileName(repoDir, "metadata.json");

   return (modelPath.FileExists() && metadataPath.FileExists());
}

void DeepModelManager::Install(ModelCard &card, ProgressCallback onProgress, CompletionHandler onCompleted)
{ 
   if (IsInstalled(card))
      return ;

   ProgressCallback progressHandler(
   [this, card, handler {std::move(onProgress)}](int64_t current, int64_t expected)
   {
      // if the install has been cancelled, bail
      // if we don't bail early, then calling the handler will 
      // segfault.
      ModelCard cardCopy = ModelCard(card);
      if (!this->IsInstalling(cardCopy))
         return;

      return handler(current, expected);
   }
   );

   // set up the install handler
   CompletionHandler installHandler(
   [this, card, handler {std::move(onCompleted)} ](int httpCode, std::string body)
   {
      // if the install has been cancelled, bail
      // if we don't bail early, then calling the handler will 
      // segfault.
      ModelCard cardCopy = ModelCard(card);
      if (!this->IsInstalling(cardCopy))
         return;

      std::string repoId = card.GetRepoID(); 
      std::string path = wxFileName(this->GetRepoDir(card), "model.pt").GetFullPath().ToStdString();

      // only attempt save if request succeeded
      if ((httpCode == 200) || (httpCode == 302))
      {
         std::istringstream modulestr(body);

         DeepModel tmpModel = DeepModel();
         tmpModel.SetCard(cardCopy);
         try
         {
            tmpModel.Load(modulestr);
            tmpModel.Save(path);
         }
         catch (const ModelException &e)
         {
            // clean up 
            Uninstall(cardCopy);
         }
      }

      // let the caller handle this
      handler(httpCode, body);

      // get rid of the cached response
      this->mResponseMap.erase(repoId);
   });

   // download the model
   try 
   {
      // save the metadata
      std::stringstream msg;
      msg<<"saving model card for "<<card.GetRepoID()<<std::endl;
      wxLogDebug(wxString(msg.str()));
      card.Save(wxFileName(GetRepoDir(card), "metadata.json").GetFullPath().ToStdString());

      msg = std::stringstream();
      msg<<"downloading model for "<<card.GetRepoID()<<std::endl;
      wxLogDebug(wxString(msg.str()));

      network_manager::ResponsePtr response = mHFWrapper.DownloadModel(card, progressHandler, installHandler);

      // add response to a temporary map (in case of cancellation)
      mResponseMap[card.GetRepoID()] = response;
   }
   catch (const char *msg)
   {
      wxLogError(msg);
      return;
   }
}

void DeepModelManager::Uninstall(ModelCard &card)
{

   wxRemoveFile(wxFileName(GetRepoDir(card), "model.pt").GetFullPath());
   wxRemoveFile(wxFileName(GetRepoDir(card), "metadata.json").GetFullPath());
   
   wxRmDir(wxFileName(GetRepoDir(card)).GetFullPath());
   
}

bool DeepModelManager::IsInstalling(ModelCard &card)
{
   return !(mResponseMap.find(card.GetRepoID()) == mResponseMap.end());
}

void DeepModelManager::CancelInstall(ModelCard &card)
{
   if (!IsInstalling(card))
    {
      // should never really reach here (can't cancel an install that's not ongoing)
      wxASSERT(false);
      return;
    }   
   else
   {
      std::string repoid = card.GetRepoID();
      network_manager::ResponsePtr response = mResponseMap[repoid];
      response->abort();
      mResponseMap.erase(repoid);
   }
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
      if (!(httpCode == 200))
      {
         // TODO: http code handling
         // how do we pass these on 
          
      }
      else
      {
         ModelCard reposCard; 
         try
         {
            reposCard = ModelCard(body);
         }
         catch (const ModelException &e)
         {
            // TODO error parsing response body
         }
         std::shared_ptr<const rapidjson::Document> reposDoc = reposCard.GetDoc();

         for (rapidjson::Value::ConstValueIterator itr = reposDoc->Begin();
                                                   itr != reposDoc->End();
                                                   ++itr)
            // TODO: need to raise an exception if we can't get the object 
            // or modelId
            repos.emplace_back(itr->GetObject()["modelId"].GetString());
      }

   };

   doGet(query, handler);

   return repos;
}

ModelCard HuggingFaceWrapper::GetCard(const std::string &repoID)
{ 
   std::string modelCardUrl = GetRootURL(repoID) + "metadata.json";
   ModelCard card = ModelCard();
   // TODO: how do you handle an exception inside a thread, like this one? 
   CompletionHandler completionHandler = [repoID, &card](int httpCode, std::string body)
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

         // std::cout<<body<<std::endl;

         // wxStringTokenizer st(wxString(repoID), wxT("/"));
         // std::string sAuthor = st.GetNextToken().ToStdString();
         // std::string sName = st.GetNextToken().ToStdString();
         // std::cout<<sName<<std::endl;

         // auto doc = rapidjson::Document();
         // rapidjson::Value author; author.SetString(sAuthor.c_str(), sAuthor.size(), doc.GetAllocator());
         // card.Set(rapidjson::Value().SetString("repo_user"), author);
         
         // rapidjson::Value name; name.SetString(sName.c_str(), sName.size(), doc.GetAllocator());
         // card.Set(rapidjson::Value().SetString("repo_name"), name);
         
      }
   };

   network_manager::ResponsePtr response = doGet(modelCardUrl, completionHandler);
   
   return card;
}

network_manager::ResponsePtr HuggingFaceWrapper::DownloadModel
(const ModelCard &card, ProgressCallback onProgress, CompletionHandler onCompleted)
{
   // TODO: this is not raising an error for an invalid URL 
   // try adding a double slash anyuwhere to break it. 
   // its because huggingface returns 200s saying "Not Found"
   std::string modelUrl = GetRootURL(card.GetRepoID())  + "model.pt";
   
   std::stringstream msg;
   msg<<"downloading from "<<modelUrl<<std::endl;
   wxLogDebug(wxString(msg.str()));

   auto response = doGet(modelUrl, onCompleted, onProgress);
   return response;
}

network_manager::ResponsePtr HuggingFaceWrapper::doGet
(std::string url, CompletionHandler completionHandler, ProgressCallback onProgress)
{
   using namespace network_manager;

   Request request(url);

   // delete me once we fix the blocking issue
   if (!onProgress)
      request.setBlocking(true);

   NetworkManager& manager = NetworkManager::GetInstance();
   ResponsePtr response = manager.doGet(request);

   // set callback for download progress
   if (onProgress)
      response->setDownloadProgressCallback(onProgress);

   response->setRequestFinishedCallback(
      [response, handler = std::move(completionHandler)](IResponse*) {
         const std::string responseData = response->readAll<std::string>();

         if (handler)
            handler(response->getHTTPCode(), responseData);
      });

   return response;
}