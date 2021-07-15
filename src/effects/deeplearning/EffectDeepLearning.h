/**********************************************************************

   Audacity: A Digital Audio Editor
   Audacity(R) is copyright (c) 1999-2021 Audacity Team.
   License: GPL v2.  See License.txt.

   EffectDeepLearning.h
   Hugo Flores Garcia

******************************************************************/
/**

\class EffectDeepLearning
\brief EffectDeepLearning provides methods for using deep learning
                            models in Audacity Effects. 

*/
/*******************************************************************/

#ifndef __AUDACITY_EFFECT_SOURCESEP__
#define __AUDACITY_EFFECT_SOURCESEP__

#include "DeepModelManager.h"
#include "DeepModel.h"
#include "../Effect.h"

// BlockIndex.first corresponds to the starting sample of a block
// BlockIndex.second corresponds to the length of the block
using BlockIndex = std::pair<sampleCount, size_t>;

class ShuttleGui;
class ModelCardPanel;

class EffectDeepLearning /* not final */ : public Effect
{
public:
   EffectDeepLearning();

   // Effect implementation

   // bool Startup() override;
   bool Init() override;
   void End() override;
   bool Process() override;
   void PopulateOrExchange(ShuttleGui & S) override;
   // bool TransferDataToWindow() override;
   // bool TransferDataFromWindow() override;

   // DeepLearningEffect implementation

   // TODO: write desc and instructions
   virtual bool ProcessOne(WaveTrack * track, double tStart, double tEnd) = 0;

   void SetModel(ModelCard card);

protected:
   // TODO: write instructions
   virtual std::string GetDeepEffectID() = 0;

   // gets the number of channels in a (possibly multichannel) track
   size_t GetNumChannels(WaveTrack *leader){return TrackList::Channels(leader).size();}

   // builds a mono tensor with shape (1, samples)
   // from a track
   torch::Tensor BuildMonoTensor(WaveTrack *track, float *buffer, 
                                 sampleCount start, size_t len);

   // builds a multichannel tensor with shape (channels, samples)
   // given a leader track.
   torch::Tensor BuildMultichannelTensor(WaveTrack *leader, float *buffer, 
                                         sampleCount start, size_t len);

   // wraps the forward pass in an exception
   torch::Tensor ForwardPass(torch::Tensor input); 

   // writes an output tensor to a track
   // tensor should be shape (1, samples)
   void TensorToTrack(torch::Tensor waveform, WaveTrack::Holder track,
                      double tStart, double tEnd);

   // returns a list of block indices. Use these to 
   // to process the audio in blocks
   std::vector<BlockIndex> GetBlockIndices(WaveTrack *track, 
                                           double tStart, double tEnd);

   // use this to update the progress ba
   int mCurrentTrackNum;
   
   // the deep model itself
   std::unique_ptr<DeepModel> mModel;

private:
   // handlers
   void OnLoadButton(wxCommandEvent &event);

private:
   ModelCard mCard;

   std::map<std::string, std::unique_ptr<ModelCardPanel>> mPanels;

   wxStaticText *mModelDesc;
};

class ModelCardPanel final : public wxPanelWrapper
{
public:
   ModelCardPanel(wxWindow *parent, wxWindowID winid, 
                  ModelCard card, EffectDeepLearning *effect);

   void PopulateOrExchange(ShuttleGui &S);
   bool TransferDataToWindow() override;
   bool TransferDataFromWindow() override;

   // calbacks
   void OnInstall(wxCommandEvent &event);
   void OnCancelInstall(wxCommandEvent &event);
   void OnUninstall(wxCommandEvent &event);

   void OnEnable(wxCommandEvent &event);

   ModelCard GetCard() const { return mCard; }

private:

   enum class InstallStatus 
   {
      uninstalled, 
      installing, 
      installed
   };

   // handlers
   void PopulateNameAndAuthor(ShuttleGui &S);
   void PopulateDescription(ShuttleGui &S);
   void PopulateMetadata(ShuttleGui &S);
   void PopulateInstallCtrls(ShuttleGui &S);

   void SetInstallStatus(InstallStatus status);

private:
   wxWindow *mParent;

   wxStaticText *mModelName;
   wxStaticText *mModelAuthor;
   wxStaticText *mModelDescription;

   wxButton *mInstallButton;
   wxStaticText *mInstallStatusText;
   wxGauge *mInstallProgressGauge;

   wxButton *mEnableButton;

   ModelCard mCard;

   EffectDeepLearning *mEffect;
};


#endif