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

   mCard.SetObject();
}

void ModelCard::InitFromFile(const std::string &path)
{
   mCard = FromFile(path);
   Validate(mCard);
}

void ModelCard::InitFromJSONString(const std::string &str)
{
   mCard = FromString(str);
   Validate(mCard);
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
      rapidjson::StringBuffer sb;
      validator.GetInvalidSchemaPointer().StringifyUriFragment(sb);
      printf("Schema Violation in: %s\n", sb.GetString());
      printf("The following schema field was violated: %s\n", validator.GetInvalidSchemaKeyword());
      sb.Clear();
      validator.GetInvalidDocumentPointer().StringifyUriFragment(sb);
      printf("Invalid document: %s\n", sb.GetString());

      throw std::exception(); //TODO:
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
      throw std::exception(); // TODO: throw a better exception
   
   return d;
}

std::string ModelCard::QueryAsString(const char *key)
{
   std::string output;
   // get the value as a string type
   if (mCard.HasMember(key))
   {
      rapidjson::StringBuffer sBuffer;
      rapidjson::Writer<rapidjson::StringBuffer> writer(sBuffer);

      mCard[key].Accept(writer);
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
   for (rapidjson::Value::ConstValueIterator itr = mCard["labels"].Begin(); 
                                             itr != mCard["labels"].End();
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

bool DeepModel::Load(const std::string &modelPath)
{
   try
   {
      // create a placeholder for our metadata string
      torch::jit::ExtraFilesMap extraFilesMap_;
      std::pair<std::string, std::string> metadata("metadata.json", "");
      extraFilesMap_.insert(metadata);

      // load the resampler module
      std::string resamplerPath = wxFileName(DLModelsDir(), wxT("resampler.ts")).GetFullPath().ToStdString(); // TODO
      mResampler = torch::jit::load(resamplerPath, torch::kCPU);

      // load the model to CPU, as well as the metadata
      mModel = torch::jit::load(modelPath, torch::kCPU,  extraFilesMap_);
      mModel.eval();

      // load the model metadata
      std::string data = extraFilesMap_["metadata.json"];
      mCard->InitFromJSONString(data);

      mSampleRate = mCard->GetDoc()["sample_rate"].GetInt();
      
      mLoaded = true;
   }
   catch (const std::exception &e)
   {
      // TODO: how to make this an audacity exception
      std::cerr << e.what() << '\n';
      mLoaded = false;
   }

   return mLoaded;
}

torch::Tensor DeepModel::Resample(const torch::Tensor &waveform, int sampleRateIn, 
                                  int sampleRateOut)
{
   if (!mLoaded) throw std::exception(); //TODO

   // set up inputs
   // torchaudio likes that sample rates are cast to float, for some reason.
   std::vector<torch::jit::IValue> inputs = {waveform, 
                                             (float)sampleRateIn, 
                                             (float)sampleRateOut};

   auto output = mResampler(inputs).toTensor();
   
   return output.contiguous();
}

// forward pass through the model!
torch::Tensor DeepModel::Forward(const torch::Tensor &waveform)
{
   torch::NoGradGuard no_grad;
   if (!mLoaded) throw std::exception(); //TODO

   // TODO: check input sizes here and throw and exception
   // if the audio is not the correct dimensions

   // set up for jit model
   std::vector<torch::jit::IValue> inputs = {waveform};

   // forward pass!
   auto tensorOutput = mModel.forward(inputs).toTensor();

   // make tensor contiguous to return to track
   tensorOutput = tensorOutput.contiguous();

   return tensorOutput;
}