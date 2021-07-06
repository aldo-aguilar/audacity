/**********************************************************************

   Audacity: A Digital Audio Editor
   Audacity(R) is copyright (c) 1999-2021 Audacity Team.
   License: GPL v2.  See License.txt.

   DeepModelManager.h
   Hugo Flores Garcia

******************************************************************/
/**

\class DeepModelManager
\brief tools for downloading and managing Audacity models hosted in HuggingFace

\class HuggingFaceWrapper
\brief API wrapper to query and download huggingface models

TODO: add a more thorough description

*/
/*******************************************************************/

#pragma once

#include "DeepModel.h"
#include "ModelCard.h"

#include "Request.h"
#include "NetworkManager.h"
#include "widgets/ProgressDialog.h"

using RepoIDList = std::vector<std::string>;
using TagList = std::vector<std::string>;
using CompletionHandler = std::function<void (int httpCode, std::string responseBody)>;
// using ProgressCallback = audacity::network_manager::ProgressCallback;

class ModelManagerException : public std::exception
{
public:
   ModelManagerException(const std::string& msg) : m_msg(msg) {}
   virtual const char* what() const throw () {return m_msg.c_str();}
   const std::string m_msg;
};

class HuggingFaceWrapper
{
   std::string GetRootURL(const std::string &repoID);

   void doGet(std::string url, CompletionHandler completionHandler, bool block = false/*, ProgressCallback progress*/);

public:
   HuggingFaceWrapper();

   // get model metadata from the hugging face model hub. 
   // repoID must be the path to a huggingface repo, e.g. "{user}/{repo_name}"
   // NOTE: the card is filled in a separate thread. 
   ModelCard GetCard(const std::string &repoID);

   // clone a model's repo
   // NOTE: would need github integration
   std::string CloneRepo(const std::string &repoID);

    // get a list of available repos that match the audacity filter. 
   RepoIDList FetchRepos();

   // download a model
   void DownloadModel(const ModelCard &card, const std::string &path);

private:
   std::string mAPIEndpoint;
};

class DeepModelManager
{
   // private! Use Get()
   DeepModelManager(); 
   ~DeepModelManager();

   FilePath GetRepoDir(const ModelCard &card);

   void FetchCards();

public:

   static DeepModelManager& Get();
   static void Shutdown();

   // base directory for deep learning models
   // TODO: maybe we want to support a couple of search paths?
   static FilePath DLModelsDir();

   // loads the deep model and passes ownership to the caller
   std::unique_ptr<DeepModel> GetModel(ModelCard &card);

   // returns the last used model card in the effect
   ModelCard GetCached(std::string &effectID);

   // download and install a deep learning model
   bool Install(ModelCard &card);
   void Uninstall(ModelCard &card);

private:
   ModelCard mSchema;
   ModelCardCollection mCards;
   std::map<std::string, ModelCard> mCachedCards;

   HuggingFaceWrapper mHFWrapper;

};

const std::string kSchemaPath = wxFileName(
   DeepModelManager::DLModelsDir(), wxT("schema.json"))
                              .GetFullPath().ToStdString();

