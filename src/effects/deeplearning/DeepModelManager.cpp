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
   repoDir.AppendDir(card["name"].GetString());

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
                           (XO("Fetching &%s...").Format(repoId)));

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
      mHFWrapper.DownloadModel(card, 
                              wxFileName(GetRepoDir(card), "model.pt").GetFullPath().ToStdString(), 
                              onProgress, 
                              onCompleted);

      // save the metadata
      // TODO: maybe have methods that return the path to card and path to model?
      card.Save(wxFileName(GetRepoDir(card), "metadata.json").GetFullPath().ToStdString());
   }
   catch (...)
   {
      //TODO: handle this
      return false;
   }

   return true;
}

void DeepModelManager::Uninstall(ModelCard &card)
{
   if (!IsInstalled(card))
      return;
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

   doGet(modelCardUrl, completionHandler);
   
   return card;
}

// TODO: maybe we want ANOTHER completion  callback that gives you something more high level
void HuggingFaceWrapper::DownloadModel(const ModelCard &card, const std::string &path, 
                                       ProgressCallback onProgress, CompletionHandler onCompleted)
{
   std::stringstream repoid;
   // TODO: we NEED to validate "author" and "name" when we fetch the modelcards. 
   // this should be done internally
   repoid<<card["author"].GetString()<<"+"<<card["name"].GetString();
   std::string modelUrl = GetRootURL(repoid.str())  + "/model.pt";

   CompletionHandler completionHandler = [path, onComplete = std::move(onCompleted), 
                                          &card](int httpCode, std::string body)
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

         onComplete(httpCode, body);

      }
   };

   doGet(modelUrl, completionHandler, onProgress);
}

void HuggingFaceWrapper::doGet(std::string url, CompletionHandler completionHandler, ProgressCallback onProgress)
{
   using namespace audacity::network_manager;

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

   return;
}