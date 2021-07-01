/**********************************************************************

   Audacity: A Digital Audio Editor
   Audacity(R) is copyright (c) 1999-2021 Audacity Team.
   License: GPL v2.  See License.txt.

   DeepModel.h
   Hugo Flores Garcia

******************************************************************/
/**

\class DeepModel
\brief base class for handling torchscript models

\class ModelCard
\brief model metadata for deep learning models

TODO: add a more thorough description

*/
/*******************************************************************/

#ifndef __AUDACITY_EFFECT_DEEPMODEL__
#define __AUDACITY_EFFECT_DEEPMODEL__

#include <torch/script.h>
#include <torch/torch.h>

#include <rapidjson/document.h>
#include <rapidjson/schema.h>
#include "rapidjson/prettywriter.h" 

using ModelLabels = std::vector<std::string>;
using CardSchema = std::unique_ptr<rapidjson::SchemaDocument>;
FilePath DLModelsDir(); 

struct ModelCard
{
private:
   rapidjson::Document mCard;
   rapidjson::Document mSchema;

   void Validate(rapidjson::Document &d);
   rapidjson::Document FromString(const std::string &str);
   rapidjson::Document FromFile(const std::string &path);
   
public:
   ModelCard();
   void InitFromFile(const std::string &str);
   void InitFromJSONString(const std::string &str);
   void InitFromHuggingFace(const std::string &repoUrl);

   // \brief queries the metadata dictionary, 
   // will convert any JSON type to a non-prettified string
   // if the key does not exist, returns "None"
   // useful for when we want to display certain 
   // model metadata to the user
   std::string QueryAsString(const char *key);
   std::vector<std::string> GetLabels();
};

class DeepModel
{
private:
   torch::jit::script::Module mModel;
   torch::jit::script::Module mResampler;
   bool mLoaded;

   // rapidjson::Document mMetadata;
   std::shared_ptr<ModelCard> mCard;

   int mSampleRate;

public:
   DeepModel();
   bool Load(const std::string &modelPath);
   bool IsLoaded(){ return mLoaded; };

   std::shared_ptr<ModelCard> GetCard(){ return mCard; };

   int GetSampleRate(){return mSampleRate;}

   torch::Tensor Resample(const torch::Tensor &waveform, int sampleRateIn, int sampleRateOut);
   torch::Tensor Forward(const torch::Tensor &waveform);
};

#endif