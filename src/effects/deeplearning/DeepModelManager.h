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

TODO: add a more thorough description

*/
/*******************************************************************/

#include "DeepModel.h"

#ifndef __AUDACITY_DEEPMODELMANAGER__
#define __AUDACITY_DEEPMODELMANAGER__

using ModelID = std::string;
using ModelMap = std::map<ModelID, std::unique_ptr<DeepModel>>;

class DeepModelManager
{
public:
   static DeepModelManager & Get();

   std::unique_ptr<DeepModel> FromHuggingFace(std::string url);

   bool RegisterModel(std::string path);

private:
   // private! Use Get()
   DeepModelManager(); 
   ~DeepModelManager();

   ModelMap mModels;
};

#endif