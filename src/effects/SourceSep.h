/**********************************************************************

   Audacity: A Digital Audio Editor
   Audacity(R) is copyright (c) 1999-2021 Audacity Team.
   License: GPL v2.  See License.txt.

   SourceSep.h
   Hugo Flores Garcia

******************************************************************/
/**

\class SourceSep

\brief SourceSep SourceSep is an Audacity Effect that performs Source Separation.
                 The goal of audio source separation is to isolate the sound sources 
                 in a given mixture of sounds. 

*/
/*******************************************************************/

#ifndef __AUDACITY_EFFECT_DEEPLEARNING__
#define __AUDACITY_EFFECT_DEEPLEARNING__

#include "deeplearning/DeepModel.h"
#include "deeplearning/EffectDeepLearning.h"
#include "Effect.h"

class wxStaticText;
class ShuttleGui;
class wxButton;

class EffectSourceSep final: public EffectDeepLearning
{
public:
   static const ComponentInterfaceSymbol Symbol;

   EffectSourceSep();
   virtual ~EffectSourceSep();

   // EffectDeepLearning implementation

   std::string GetDeepEffectID() override;

   // ComponentInterface implementation

   ComponentInterfaceSymbol GetSymbol() override;
   TranslatableString GetDescription() override;
   wxString ManualPage() override;

   // EffectDefinitionInterface implementation

   EffectType GetType() override;

   // Effect implementation
   bool ProcessOne(WaveTrack * track, double tStart, double tEnd) override;

private:

   std::vector<WaveTrack::Holder> CreateSourceTracks(WaveTrack *track, 
                                             std::vector<std::string> &labels);
   void PostProcessSources(std::vector<WaveTrack::Holder> &sourceTracks, 
                           sampleFormat fmt, int sampleRate);

};

#endif