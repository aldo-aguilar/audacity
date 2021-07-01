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

using ModulePtr = std::unique_ptr<torch::jit::script::Module>;
using ModelLabels = std::vector<std::string>;
using CardSchema = std::unique_ptr<rapidjson::SchemaDocument>;
FilePath DLModelsDir(); 

class ModelException : public std::exception
{
public:
   ModelException(const std::string& msg) : m_msg(msg) {}
   virtual const char* what() const throw () {return m_msg.c_str();}
   const std::string m_msg;
};

class ModelCard
{
private:
   rapidjson::Document mDoc;
   rapidjson::Document mSchema;

   void Validate(rapidjson::Document &d);
   rapidjson::Document FromString(const std::string &str);
   rapidjson::Document FromFile(const std::string &path);
   
public:
   ModelCard();

   // all of these may throw a ModelException if
   // the input document does not match the model schema
   void InitFromFile(const std::string &path);
   void InitFromJSONString(const std::string &str);

   // \brief queries the metadata dictionary, 
   // will convert any JSON type to a non-prettified string
   // if the key does not exist, returns "None"
   // useful for when we want to display certain 
   // metadata fields to the user, even if the doc is empty.
   std::string QueryAsString(const char *key);

   // returns the labels associated with the model. 
   std::vector<std::string> GetLabels();

   // get a copy of the JSON document object. 
   rapidjson::Document GetDoc();
};

class DeepModel
{
private:
   ModulePtr mModel;
   ModulePtr mResampler;

   // rapidjson::Document mMetadata;
   std::shared_ptr<ModelCard> mCard;

   int mSampleRate;
   bool mLoaded;

   void Cleanup();

public:
   DeepModel();

   // @execsafety: strong 
   // load a torchscript model along with it's metadata, 
   // which is stored in a ModelCard.
   void Load(const std::string &modelPath);
   bool IsLoaded(){ return mLoaded; };

   // use the ModelCard to access metadata attribute's in the 
   // models metadata.json file. 
   std::shared_ptr<ModelCard> GetCard(){ return mCard; };
   int GetSampleRate(){return mSampleRate;}

   // @execsafety: strong (may throw if model is not loaded or 
   // forward pass fails)
   // waveform should be shape (channels, samples)
   torch::Tensor Resample(const torch::Tensor &waveform, int sampleRateIn, int sampleRateOut);

   // @execsafety: strong (may throw if model is not loaded or 
   // forward pass fails)
   // waveform should be shape (channels, samples)
   torch::Tensor Forward(const torch::Tensor &waveform);
};

#endif