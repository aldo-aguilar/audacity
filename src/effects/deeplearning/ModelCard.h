/**********************************************************************

   Audacity: A Digital Audio Editor
   Audacity(R) is copyright (c) 1999-2021 Audacity Team.
   License: GPL v2.  See License.txt.

   ModelCard.h
   Hugo Flores Garcia

******************************************************************/
/**

\class ModelCard
\brief model metadata for deep learning models

*/
/*******************************************************************/

#pragma once

#include <torch/script.h>
#include <torch/torch.h>

#include <rapidjson/document.h>
#include <rapidjson/schema.h>
#include "rapidjson/prettywriter.h" 

#include "MemoryX.h"

// wrapper for a shared_ptr to a JSON document. 
class ModelCard
{
private:
   static rapidjson::Document FromString(const std::string &str);
   static rapidjson::Document FromFile(const std::string &path);
   
   ModelCard(std::shared_ptr<rapidjson::Document> doc);

public:
   // all constructors may throw if the provided JSON doc does not match the schema
   // TODO: make sure exec safety is strong?
   ModelCard();
   // initialize from a JSON string
   ModelCard(const std::string &JSONstr);

   // make a copy of the internal JSON document
   ModelCard DeepCopy() const;

   // maythrow
   // const 
   // TODO: exec safety
   // TODO: docs
   void Save(const std::string &path) const;

   // initialize from a JSON file
   static ModelCard CreateFromFile(const std::string &path);

   // validate the model card with a given schema
   bool IsValid(const rapidjson::Document &schema) const; // doesn't throw
   void Validate(const rapidjson::Document &schema) const; // throws 
   
   // alternative constructor (from file)
   static ModelCard InitFromFile(const std::string &path);

   // returns the labels associated with the model. 
   std::vector<std::string> GetLabels() const;

   // get a view of the JSON document object. 
   std::shared_ptr<const rapidjson::Document> GetDoc() const;

   // may throw if doesn't exist
   // use this to access metadata values
   // @execsafety strong
   rapidjson::Value &operator[](const char *name) const;

   // all cards that have the same repoID (name and author)
   // will be marked as equal
   // TODO: add versioning?
   bool operator==(const ModelCard& that);
   bool operator!=(const ModelCard& that);

   // returns {author}/{name}
   std::string GetRepoID() const;


private:
   std::shared_ptr<rapidjson::Document> mDoc;

};

using ModelCardFilter = std::function<bool(const ModelCard &card)>;

class ModelCardCollection
{
public:
   // set the JSON schema to be used when Validate() is called. 
   ModelCardCollection(ModelCard schema);

   // insert a model card into the collection if it matches the schema
   // will throw if the card does not match the schema
   // only adds if the card isn't already there (doesn't throw)
   // @execsafety strong
   void Insert(ModelCard &card);

   // returns an empty copy, but with the appropriate schema
   ModelCardCollection EmptyCopy();
   // returns a view of a subset as dictated by the filter
   ModelCardCollection Filter(ModelCardFilter filter);

   // use this to grab a view of the subset which satisfies a new schema;
   ModelCardCollection FilterBySchema(std::shared_ptr<rapidjson::Document> schema);

   // returns an iterator to the cards
   std::vector<ModelCard>::iterator begin() {return mCards.begin();}
   std::vector<ModelCard>::iterator end() {return mCards.end();}
   size_t Size() {return mCards.size();}

   // ModelCard Find(std::string repoID) { return std::find(, repoID);}

private:
   std::vector<ModelCard> mCards;
   ModelCard mSchema;

};