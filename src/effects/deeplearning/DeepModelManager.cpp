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

   std::cout<<card["author"].GetString()<<std::endl;
   std::cout<<card["name"].GetString()<<std::endl;

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
         std::cerr<<msg.str()<<std::endl;

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
         std::cerr<<msg.str()<<std::endl;
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
         msg<<e.what()<<std::endl;\
         std::cerr<<msg.str()<<std::endl;

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
         std::cerr<<msg.str()<<std::endl;
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

bool DeepModelManager::Install(ModelCard &card, ProgressCallback onProgress, CompletionHandler onCompleted)
{ 
   if (IsInstalled(card))
      return true;

   // download the model
   try 
   {
      // save the metadata
      // TODO: maybe have methods that return the path to card and path to model?
      std::cout<<"saving model card for "<<card.GetRepoID();
      card.Save(wxFileName(GetRepoDir(card), "metadata.json").GetFullPath().ToStdString());

      std::cout<<"downloading model for "<<card.GetRepoID();
      network_manager::ResponsePtr response = mHFWrapper.DownloadModel(
               card, card.GetRepoID(), wxFileName(GetRepoDir(card), "model.pt").GetFullPath().ToStdString(), 
               onProgress, onCompleted);
      mResponseMap[card.GetRepoID()] = response;
   }
   catch (const char *msg)
   {
      //TODO: handle this
      std::cerr<<msg<<std::endl;
      return false;
   }

   return true;
}

void DeepModelManager::Uninstall(ModelCard &card)
{
   if (!IsInstalled(card))
      return;

   wxRemoveFile(wxFileName(GetRepoDir(card), "model.pt").GetFullPath());
   wxRemoveFile(wxFileName(GetRepoDir(card), "metadata.json").GetFullPath());
   
   wxFileName(GetRepoDir(card)).RemoveLastDir();
}

void DeepModelManager::CancelInstall(ModelCard &card)
{
   if (mResponseMap.find(card.GetRepoID()) == mResponseMap.end())
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
(const ModelCard &card, const std::string &repoID, const std::string &path, 
 ProgressCallback onProgress, CompletionHandler onCompleted)
{
   // TODO: this is not raising an error for an invalid URL 
   // try adding a double slash anyuwhere to break it. 
   // its because huggingface returns 200s saying "Not Found"
   std::string modelUrl = GetRootURL(repoID)  + "model.pt";
   std::cout<<modelUrl<<std::endl;

   CompletionHandler completionHandler = [path, onComplete = std::move(onCompleted), 
                                          card](int httpCode, std::string body)
   {
      // looks like models can also return a 302 and succeed
      if (!(httpCode == 200 || httpCode == 302))
      {
         std::stringstream msg;
         msg << "GET request failed. Error code: " << httpCode;
         std::cerr<<msg.str()<<std::endl;
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

         onComplete(httpCode, body);

      }
   };

   auto response = doGet(modelUrl, completionHandler, onProgress);
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