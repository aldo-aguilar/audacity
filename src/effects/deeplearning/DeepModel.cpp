/**********************************************************************

   Audacity: A Digital Audio Editor
   Audacity(R) is copyright (c) 1999-2021 Audacity Team.
   License: GPL v2.  See License.txt.

   DeepModel.cpp
   Hugo Flores Garcia

******************************************************************/

#include "DeepModel.h"

#include <torch/script.h>
#include <torch/torch.h>

#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>
#include "rapidjson/schema.h"
#include "rapidjson/error/en.h"

#include "../../WaveTrack.h"
#include "../../WaveClip.h"

FilePath DLModelsDir()
{
   wxFileName modelsDir(FileNames::BaseDir(), wxEmptyString);
   modelsDir.AppendDir(wxT("deeplearning-models"));
   return modelsDir.GetFullPath();
}

// ModelCard Implementation

ModelCard::ModelCard()
{
   // load the schema
   std::string schemaPath = wxFileName(DLModelsDir(), wxT("schema.json")).GetFullPath().ToStdString(); // TODO
   mSchema = FromFile(schemaPath);

   mDoc.SetObject();
}

void ModelCard::InitFromFile(const std::string &path)
{
   mDoc = FromFile(path);
   Validate(mDoc);
}

void ModelCard::InitFromJSONString(const std::string &str)
{
   mDoc = FromString(str);
   Validate(mDoc);
}

void ModelCard::Validate(rapidjson::Document &d)
{
   rapidjson::SchemaDocument schema(mSchema);
   rapidjson::SchemaValidator validator(schema);

   validator.Reset();
   if (!d.Accept(validator)) 
   {
      // Input JSON is invalid according to the schema
      // Output diagnostic information
      std::string message("A Schema violation was found in the Model Card.\n");

      rapidjson::StringBuffer sb;
      validator.GetInvalidSchemaPointer().StringifyUriFragment(sb);
      message += "violation found in: " + std::string(sb.GetString()) + "\n";
      message += "the following schema field was violated: " 
                  + std::string(validator.GetInvalidSchemaKeyword()) + "\n";
      sb.Clear();

      message += "invalid document: \n\t" 
                  + std::string(d.GetString()) + "\n";
      message += "schema document: \n\t" 
                  + std::string(mSchema.GetString()) + "\n";

      throw ModelException(message);
   }
}

// https://rapidjson.org/md_doc_stream.html
rapidjson::Document ModelCard::FromFile(const std::string &path)
{
   FILE* fp = fopen(path.c_str(), "r");

   char readBuffer[65536];
   rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));

   rapidjson::Document d;
   d.ParseStream(is);

   fclose(fp);

   return d;
}

rapidjson::Document ModelCard::FromString(const std::string &data)
{
   rapidjson::Document d;
   // parse the data
   d.Parse(data.c_str());
   if (d.Parse(data.c_str()).HasParseError()) 
   {
      std::string msg = "error parsing JSON from string: \n";
      msg += rapidjson::GetParseError_En(d.GetParseError());
      msg += "\n document: \n\t" + std::string(d.GetString()) + "\n";
      throw ModelException(msg);
   }
   
   return d;
}

std::string ModelCard::QueryAsString(const char *key)
{
   std::string output;
   // get the value as a string type
   if (mDoc.HasMember(key))
   {
      rapidjson::StringBuffer sBuffer;
      rapidjson::Writer<rapidjson::StringBuffer> writer(sBuffer);

      mDoc[key].Accept(writer);
      output = sBuffer.GetString();
   }
   else
   {
      output = "None";
   }
   return std::string(output);
}

std::vector<std::string> ModelCard::GetLabels()
{
   // iterate through the labels and collect
   std::vector<std::string> labels;
   for (rapidjson::Value::ConstValueIterator itr = mDoc["labels"].Begin(); 
                                             itr != mDoc["labels"].End();
                                             ++itr)
   {
      labels.emplace_back(itr->GetString());
   }

   return labels;
}

rapidjson::Document ModelCard::GetDoc()
{
   rapidjson::Document::AllocatorType& allocator = mDoc.GetAllocator();

   rapidjson::Document copy;
   copy.CopyFrom(mDoc, allocator);

   if (!copy.IsObject()) copy.SetObject();

   return copy;
}

// DeepModel Implementation

DeepModel::DeepModel() : mLoaded(false), mCard(std::make_shared<ModelCard>()){}

void DeepModel::Load(const std::string &modelPath)
{
   try
   {
      // create a placeholder for our metadata string
      torch::jit::ExtraFilesMap extraFilesMap_;
      std::pair<std::string, std::string> metadata("metadata.json", "");
      extraFilesMap_.insert(metadata);

      // load the resampler module
      std::string resamplerPath = wxFileName(DLModelsDir(), wxT("resampler.pt")).GetFullPath().ToStdString(); // TODO
      mResampler = std::make_unique<torch::jit::script::Module>
                     (torch::jit::load(resamplerPath, torch::kCPU));
      mResampler->eval();

      // load the model to CPU, as well as the metadata
      mModel = std::make_unique<torch::jit::script::Module>
                     (torch::jit::load(modelPath, torch::kCPU,  extraFilesMap_));
      mModel->eval();

      // load the model metadata
      std::string data = extraFilesMap_["metadata.json"];
      mCard->InitFromJSONString(data);

      // set sample rate
      mSampleRate = mCard->GetDoc()["sample_rate"].GetInt();
      
      // finally, mark as loaded
      mLoaded = true;
   }
   catch (const std::exception &e)
   {
      Cleanup();
      throw ModelException(e.what());
   }
}

void DeepModel::Cleanup()
{
   // cleanup
   mCard.reset();
   mModel.reset();
   mLoaded = false;
}

torch::Tensor DeepModel::Resample(const torch::Tensor &waveform, int sampleRateIn, 
                                  int sampleRateOut)
{
   if (!mLoaded) throw ModelException("Attempted resample while is not loaded."
                                       " Please call Load() first."); 

   // set up inputs
   // torchaudio likes that sample rates are cast to float, for some reason.
   std::vector<torch::jit::IValue> inputs = {waveform, 
                                             (float)sampleRateIn, 
                                             (float)sampleRateOut};

   torch::Tensor output;
   try
   {
      output = mResampler->forward(inputs).toTensor();
   }
   catch (const std::exception &e)
   {
      throw ModelException(e.what());
   }

   return output.contiguous();
}

// forward pass through the model!
torch::Tensor DeepModel::Forward(const torch::Tensor &waveform)
{
   torch::NoGradGuard no_grad;
   if (!mLoaded) throw ModelException("Attempted forward pass while model is not loaded."
                                       " Please call Load() first."); 

   // set up for jit model
   std::vector<torch::jit::IValue> inputs = {waveform};

   // forward pass!
   torch::Tensor output;
   try
   {
      output = mModel->forward(inputs).toTensor();
   }
   catch (const std::exception &e)
   {
      throw ModelException(e.what());
   }
   // make tensor contiguous to return to track
   output = output.contiguous();

   return output;
}