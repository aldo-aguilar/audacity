/**********************************************************************

   Audacity: A Digital Audio Editor
   Audacity(R) is copyright (c) 1999-2021 Audacity Team.
   License: GPL v2.  See License.txt.

   ModelCard.cpp
   Hugo Flores Garcia

******************************************************************/

#include "ModelCard.h"
#include "DeepModel.h"

#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>
#include "rapidjson/schema.h"
#include "rapidjson/error/en.h"

// ModelCard Implementation

ModelCard::ModelCard()
{
   // add an empty card
   std::shared_ptr<rapidjson::Document> doc = std::make_shared<rapidjson::Document>();
   doc->SetObject();
   (*this) = ModelCard(doc);
}

ModelCard::ModelCard(std::shared_ptr<rapidjson::Document> doc) : mDoc(doc) { }

ModelCard::ModelCard(const std::string &JSONstr)
{
   std::shared_ptr<rapidjson::Document> doc = std::make_shared<rapidjson::Document>(FromString(JSONstr));
   *this = ModelCard(doc);
}

ModelCard ModelCard::CreateFromFile(const std::string &path)
{
   auto doc = std::make_shared<rapidjson::Document>(FromFile(path));
   return ModelCard(doc);
}

bool ModelCard::IsValid(const rapidjson::Document &schema)
{
   rapidjson::SchemaDocument schemaDoc(schema);
   rapidjson::SchemaValidator validator(schemaDoc);

   return (!mDoc->Accept(validator));
}

void ModelCard::Validate(const rapidjson::Document &schema)
{
   rapidjson::SchemaDocument schemaDoc(schema);
   rapidjson::SchemaValidator validator(schemaDoc);

   if (!mDoc->Accept(validator)) 
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
                  + std::string(mDoc->GetString()) + "\n";
      message += "schema document: \n\t" 
                  + std::string(schema.GetString()) + "\n";

      throw ModelException(message);
   }
}

rapidjson::Document ModelCard::FromFile(const std::string &path)
{
   // https://rapidjson.org/md_doc_stream.html
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
   if (mDoc->HasMember(key))
   {
      rapidjson::StringBuffer sBuffer;
      rapidjson::Writer<rapidjson::StringBuffer> writer(sBuffer);

      (*mDoc)[key].Accept(writer);
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
   for (rapidjson::Value::ConstValueIterator itr = (*mDoc)["labels"].Begin(); 
                                             itr != (*mDoc)["labels"].End();
                                             ++itr)
   {
      labels.emplace_back(itr->GetString());
   }

   return labels;
}

std::shared_ptr<rapidjson::Document> ModelCard::GetDoc()
{
   return mDoc;
}

rapidjson::Value& ModelCard::operator[](const char *name) const{ 
   // wxASSERT(mDoc->HasMember(name); // TODO: what kind of exception do I want to raise here
   if (!mDoc->HasMember(name))
      throw ModelException("Invalid Model Card field: " + std::string(name));
   return mDoc->operator[](name);
};

// ModelCardCollection implementation

ModelCardCollection::ModelCardCollection(std::shared_ptr<rapidjson::Document> schema)
{
   mSchema = schema;
}

bool ModelCardCollection::Insert(ModelCard &card)
{
   bool result = true;
   if (card.IsValid(*mSchema))
      mCards.push_back(card);
   else
      result = false;

   return result;
}

ModelCardCollection ModelCardCollection::Filter(ModelCardFilter filter)
{
   ModelCardCollection that = EmptyCopy();
   std::vector<ModelCard>::iterator it = std::copy_if(this->begin(), this->end(), that.begin(), filter);
   that.mCards.resize(std::distance(that.begin(), it));

   return that;
}

ModelCardCollection ModelCardCollection::EmptyCopy()
{
   ModelCardCollection that = EmptyCopy();
   that.mCards = std::vector<ModelCard>(mCards.size());
   return that;
}