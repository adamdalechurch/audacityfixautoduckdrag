/**********************************************************************

  Audacity: A Digital Audio Editor

  @file LV2InstanceFeaturesList.h

  Paul Licameli split from LV2Effect.h

  Audacity(R) is copyright (c) 1999-2013 Audacity Team.
  License: GPL v2 or later.  See License.txt.

*********************************************************************/

#ifndef __AUDACITY_LV2_INSTANCE_FEATURES_LIST__
#define __AUDACITY_LV2_INSTANCE_FEATURES_LIST__

#if USE_LV2

#include "LV2FeaturesList.h"
#include "lv2/options/options.h"
#include "lv2/worker/worker.h"

struct LV2InstanceFeaturesList final : ExtendedLV2FeaturesList {
   explicit LV2InstanceFeaturesList(
      const LV2FeaturesList &baseFeatures, float sampleRate = 44100,
      const LV2_Worker_Schedule *pWorkerSchedule = nullptr);

   //! @return success
   bool InitializeOptions();

   //! @return may be null
   const LV2_Options_Option *NominalBlockLengthOption() const;

   size_t AddOption(LV2_URID, uint32_t size, LV2_URID, const void *value);

   /*!
    @param subject URI of a plugin
    @return whether all required options of subject are supported
    */
   bool ValidateOptions(const LilvNode *subject);

   /*!
    @param subject URI of a plugin
    @param required whether to check required or optional options of subject
    @return true only if `!required` or else all required options are supported
    */
   bool CheckOptions(const LilvNode *subject, bool required);

   std::vector<LV2_Options_Option> mOptions;
   size_t mBlockSizeOption{};

   size_t mBlockSize{ LV2Preferences::DEFAULT_BLOCKSIZE };
   int mSeqSize{ DEFAULT_SEQSIZE };

   bool mSupportsNominalBlockLength{ false };

   size_t mMinBlockSize{ 1 };
   size_t mMaxBlockSize{ mBlockSize };
   const float mSampleRate;

   const bool mOk;
};

#endif
#endif
