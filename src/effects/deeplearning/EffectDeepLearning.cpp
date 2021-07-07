/**********************************************************************

   Audacity: A Digital Audio Editor
   Audacity(R) is copyright (c) 1999-2021 Audacity Team.
   License: GPL v2.  See License.txt.

   EffectDeepLearning.cpp
   Hugo Flores Garcia

******************************************************************/
/**

*/
/*******************************************************************/

#include "EffectDeepLearning.h"

#include <torch/script.h>

EffectDeepLearning::EffectDeepLearning()
{ 
   // create an empty deep model
   mModel = std::make_unique<DeepModel>();
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
