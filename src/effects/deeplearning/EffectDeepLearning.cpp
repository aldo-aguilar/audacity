/**********************************************************************

   Audacity: A Digital Audio Editor
   Audacity(R) is copyright (c) 1999-2021 Audacity Team.
   License: GPL v2.  See License.txt.

   EffectDeepLearning.cpp
   Hugo Flores Garcia

******************************************************************/

#include "EffectDeepLearning.h"

#include "FileNames.h"
#include "Shuttle.h"
#include "ShuttleGui.h"


#include <torch/script.h>

// register event handlers
// BEGIN_EVENT_TABLE(EffectSourceSep, wxEvtHandler)
//    EVT_BUTTON(wxID_ANY, EffectSourceSep::OnLoadButton)
// END_EVENT_TABLE()

// ModelCardPanel


EffectDeepLearning::EffectDeepLearning()
{
   mCardPanel = NULL;
}

bool EffectDeepLearning::Init()
{
   DeepModelManager &manager = DeepModelManager::Get();

   std::unique_ptr<ProgressDialog> progress =  std::make_unique<ProgressDialog>(XO("Loading Model Manager..."));

   manager.FetchCards(progress.get());

   // std::string effectid = GetDeepEffectID(); //TODO: maybe we want an enum for the effect id?
   // mCard = manager.GetCached(effectid);
   // mModel = manager.GetModel(mCard);

   // TODO: except handling
   mModel = std::make_unique<DeepModel>();
   return true; 
}

void EffectDeepLearning::End()
{
   // release model
   mModel.reset();
}

bool EffectDeepLearning::Process()
{
   // throw an error if there isn't a model loaded
   if (!mModel->IsLoaded())
   {
      Effect::MessageBox(
         XO("Please load a model before applying the effect."),
         wxICON_ERROR );
      return false;
   }

   // Iterate over each track.
   // All needed because this effect needs to introduce
   // silence in the sync-lock group tracks to keep sync
   CopyInputTracks(true); // Set up mOutputTracks.
   bool bGoodResult = true;

   mCurrentTrackNum = 0;

   // NOTE: because we will append the separated tracks to mOutputTracks in ProcessOne(), 
   // we need to collect the track pointers before calling ProcessOne()
   std::vector< WaveTrack* > pOutLeaders;
   for ( WaveTrack *track : mOutputTracks->SelectedLeaders< WaveTrack >())
      pOutLeaders.emplace_back(track);

   // now that we have all the tracks we want to process, 
   // go ahead and process each!
   for ( WaveTrack* pOutLeader : pOutLeaders) {

      //Get start and end times from track
      double tStart = pOutLeader->GetStartTime();
      double tEnd = pOutLeader->GetEndTime();

      //Set the current bounds to whichever left marker is
      //greater and whichever right marker is less:
      tStart = wxMax(mT0, tStart);
      tEnd = wxMin(mT1, tEnd);

      // Process only if the right marker is to the right of the left marker
      if (tEnd > tStart) {
         //ProcessOne() (implemented below) processes a single track
         if (!ProcessOne(pOutLeader, tStart, tEnd))
            bGoodResult = false;
      }
      // increment current track
      mCurrentTrackNum++;
   }

   ReplaceProcessedTracks(bGoodResult);

   return bGoodResult;
}

// gets a list of starting samples and block lengths 
// dictated by the track, so we can process the audio
// audio in blocks
std::vector<BlockIndex> EffectDeepLearning::GetBlockIndices
(WaveTrack *track, double tStart, double tEnd)
{
   std::vector<BlockIndex> blockIndices;

   sampleCount start = track->TimeToLongSamples(tStart);
   sampleCount end = track->TimeToLongSamples(tEnd);

   //Get the length of the selection (as double). len is
   //used simple to calculate a progress meter, so it is easier
   //to make it a double now than it is to do it later
   double len = (end - start).as_double();

   //Go through the track one buffer at a time. samplePos counts which
   //sample the current buffer starts at.
   bool bGoodResult = true;
   sampleCount samplePos = start;
   while (samplePos < end) 
   {
      //Get a blockSize of samples (smaller than the size of the buffer)
      size_t blockSize = limitSampleBufferSize(
         /*bufferSize*/ track->GetBestBlockSize(samplePos),
         /*limit*/ end - samplePos
      );

      blockIndices.emplace_back(BlockIndex(samplePos, blockSize));

      // Increment the sample pointer
      samplePos += blockSize;
   }

   return blockIndices;
}

// TODO: get rid of the Floats entirely and simply pass the data_ptr
// to empty torch contiguous zeros
torch::Tensor EffectDeepLearning::BuildMonoTensor(WaveTrack *track, float *buffer, 
                              sampleCount start, size_t len)
{
   //Get the samples from the track and put them in the buffer
   if (!track->GetFloats(buffer, start, len))
      throw std::runtime_error("An error occurred while copying samples to tensor buffer.");

   // get tensor input from buffer
   torch::Tensor audio = torch::from_blob(buffer, len, 
                                          torch::TensorOptions().dtype(torch::kFloat));
   audio = audio.unsqueeze(0); // add channel dimension

   return audio;
}

torch::Tensor EffectDeepLearning::BuildMultichannelTensor(WaveTrack *leader, float *buffer,
                                                      sampleCount start, size_t len)
{
   auto channels = TrackList::Channels(leader);
   std::vector<torch::Tensor> channelStack;

   // because we're reusing the same buffer, it's important that
   // we clone the tensor. 
   for (WaveTrack *channel : channels)
      channelStack.emplace_back(
         BuildMonoTensor(channel, buffer, start, len).clone()
      );

   return torch::stack(channelStack);
}

torch::Tensor EffectDeepLearning::ForwardPass(torch::Tensor input)
{
   torch::Tensor output;
   try
   {
      output = mModel->Forward(input);
   }
   catch (const std::exception &e)
   {
      // TODO: what do 
      std::cerr<<e.what();
      Effect::MessageBox(XO("An error occurred during the forward pass"
                             "This model may be broken."),
         wxOK | wxICON_ERROR
      );

      output = torch::zeros_like(input);
   }
   return output;
}

void EffectDeepLearning::TensorToTrack(torch::Tensor waveform, WaveTrack::Holder track, 
                                   double tStart, double tEnd)
{
   if (!(waveform.size(0) == 1))
      throw std::runtime_error("Input waveform tensor should be shape (1, samples)"); 

   // get the data pointer
   float *data = waveform.contiguous().data_ptr<float>();
   size_t outputLen = waveform.size(-1);

   // add the data to a temporary track, then 
   // paste on our output track
   WaveTrack::Holder tmp = track->EmptyCopy();
   tmp->Append((samplePtr)data, floatSample, outputLen);
   tmp->Flush();

   try {
      track->ClearAndPaste(tStart, tEnd, tmp.get());
   }
   catch (const std::exception &e)
   { 
      Effect::MessageBox(XO("Error copying tensor data to output track"),
      wxOK | wxICON_ERROR 
      ); 
   }
}

// UI stuff
void EffectDeepLearning::PopulateOrExchange(ShuttleGui &S)
{
   DeepModelManager &manager = DeepModelManager::Get();

   S.StartVerticalLay(wxCENTER, true);
   {

      wxScrolledWindow *scroller = S.Style(wxVSCROLL | wxTAB_TRAVERSAL)
                                    .StartScroller();
      {
         wxWindow *parent = S.GetParent();
         mCard = *manager.GetCards().begin();

         for (auto &card : manager.GetCards())
         { 
            auto panel = std::make_unique<ModelCardPanel>(S.GetParent(), wxID_ANY, card);
            panel->PopulateOrExchange(S);
         }
      }
      S.EndScroller();

      S.StartHorizontalLay(wxCENTER, false);
      {
         std::string modelDesc;
         if (mModel->IsLoaded()) 
            modelDesc = "Ready";
         else 
            modelDesc = "Not Ready";

         S.AddVariableText(TranslatableString(wxString(modelDesc).c_str(), {}));
         S.AddVariableText(XO("why hello"));
      }
      S.EndHorizontalLay();
   }
   S.EndVerticalLay();
}

// bool EffectDeepLearning::TransferDataToWindow()
// {
//    if (!mUIParent->TransferDataToWindow()) // HUGO: might wanna use mUIParent to pull up the manager window
//    {
//       return false;
//    }

//    mCardPanel->Refresh(false);

//    return true;
// }

// bool EffectDeepLearning::TransferDataFromWindow()
// {
//    if (!mUIParent->Validate() || !mUIParent->TransferDataFromWindow())
//    {
//       return false;
//    }

//    return true;
// }

// ModelCardPanel 

enum {
   ID_INSTALLBUTTON = 10000,
   ID_INSTALLSTATUS,
   ID_INSTALLPROGRESS,
   ID_MODELDESCRIPTION,
   ID_MODELAUTHOR,
   ID_MODELNAME
};

BEGIN_EVENT_TABLE(ModelCardPanel, wxEvtHandler)
   EVT_BUTTON(ID_INSTALLBUTTON, ModelCardPanel::OnInstall)
END_EVENT_TABLE()   

ModelCardPanel::ModelCardPanel(wxWindow *parent, wxWindowID winid, ModelCard card)
                  :wxPanelWrapper(parent, winid, wxDefaultPosition, wxSize(300, 150))
{
   SetLabel(XO("Model Card"));
   SetName(XO("Model Card"));

   mParent = parent;
   mCard = card;

   // ShuttleGui S(this, eIsCreating);
   // PopulateOrExchange(S);
}

void ModelCardPanel::PopulateNameAndAuthor(ShuttleGui &S)
{
   // {repo-name}
   mModelName = S.Id(ID_MODELNAME)
                  .AddVariableText(XO("%s")
                                    .Format(mCard["name"].GetString()),
                                       false, wxLEFT);
   mModelName->SetFont(wxFont(wxFontInfo().Bold()));
   
   // by {author}
   S.StartHorizontalLay(wxALIGN_LEFT, true);
   {
      S.AddVariableText(XO("by"));
      mModelAuthor = S.Id(ID_MODELAUTHOR)
                        .AddVariableText(XO("%s")
                                       .Format(mCard["author"].GetString()));
      mModelAuthor->SetFont(wxFont(wxFontInfo().Bold()));
                                       
   }
   S.EndHorizontalLay();
}

void ModelCardPanel::PopulateDescription(ShuttleGui &S)
{
   // model description
   S.StartStatic(XO("Description"));
   mModelDescription = S.Id(ID_MODELDESCRIPTION)
                        .AddVariableText(XO("%s")
                              .Format(wxString(mCard["description"].GetString())), 
                              false, wxLEFT);
   S.EndStatic();
}

void ModelCardPanel::PopulateMetadata(ShuttleGui &S)
{
   S.StartMultiColumn(2, wxCENTER);
   {
      S.AddVariableText(XO("Effect: "))
                        ->SetFont(wxFont(wxFontInfo().Bold()));
      S.AddVariableText(XO("%s")
                        .Format(mCard["effect"].GetString()));

      S.AddVariableText(XO("Domain: "))
                        ->SetFont(wxFont(wxFontInfo().Bold()));
      S.AddVariableText(XO("%s")
                        .Format(mCard["domain"].GetString()));
      
      S.AddVariableText(XO("Sample Rate: "))
                        ->SetFont(wxFont(wxFontInfo().Bold()));
      S.AddVariableText(XO("%d")
                        .Format(mCard["sample_rate"].GetInt()));
   }
   S.EndMultiColumn();
}

void ModelCardPanel::PopulateInstallCtrls(ShuttleGui &S)
{
   DeepModelManager &manager = DeepModelManager::Get();

   S.StartVerticalLay(wxCENTER, true);
   {  
      // add a progress gauge for downloads, but hide it
      mInstallProgressGauge = safenew wxGauge(S.GetParent(), wxID_ANY, 100);
      S.Id(ID_INSTALLPROGRESS).AddWindow(mInstallProgressGauge);
      mInstallProgressGauge->Hide();
                              
      S.StartHorizontalLay(wxCENTER, true);
      {
         bool installed = manager.IsInstalled(mCard);
         std::string status = installed ? "installed" : "uninstalled";
         mInstallStatusText = S.Id(ID_INSTALLSTATUS)
                                 .AddVariableText(XO("%s").Format(status));

         wxColour statusColor = installed ? *wxGREEN : *wxRED;
         mInstallStatusText->SetForegroundColour(statusColor);

         S.Id(ID_INSTALLBUTTON)
            .AddButton(XXO("&Install"));
      }
      S.EndHorizontalLay();
   }
   S.EndVerticalLay();
}

void ModelCardPanel::PopulateOrExchange(ShuttleGui &S)
{
   DeepModelManager &manager = DeepModelManager::Get();

   // the layout is actually 2 columns, 
   // but we add a small space in the middle, which takes up a column
   S.StartStatic(XO(""), 1);
   S.StartMultiColumn(3, wxEXPAND);
   {
      // left column: 
      // repo name, repo author, and model description
      S.SetStretchyCol(0);
      S.StartVerticalLay(wxALIGN_LEFT, true);
      {
         PopulateNameAndAuthor(S);

         PopulateDescription(S);
      }
      S.EndVerticalLay();

      // dead space (center column)
      S.AddSpace(5, 0);

      S.StartMultiColumn(1);
      {
         // top: other model metadata
         S.StartVerticalLay(wxALIGN_TOP, false);
         {
            PopulateMetadata(S);
         }
         S.EndVerticalLay();

         // bottom: install and uninstall controls
         S.StartVerticalLay(wxALIGN_BOTTOM, false);
         {
            S.StartHorizontalLay(wxALIGN_RIGHT);
            { 
               PopulateInstallCtrls(S);
            }
            S.EndHorizontalLay();

         }
         S.EndVerticalLay();

      }
      S.EndVerticalLay();
   }
   S.EndMultiColumn();
   S.EndStatic();
}

void ModelCardPanel::OnInstall(wxCommandEvent &event)
{
   DeepModelManager &manager = DeepModelManager::Get();
}