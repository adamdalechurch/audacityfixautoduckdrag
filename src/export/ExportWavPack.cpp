/**********************************************************************

   SPDX-License-Identifier: GPL-2.0-or-later

   Audacity: A Digital Audio Editor

   ExportWavPack.cpp

   Subhradeep Chakraborty

   Based on ExportOGG.cpp, ExportMP2.cpp by:
   Joshua Haberman
   Markus Meyer

**********************************************************************/


#include "Export.h"
#include "wxFileNameWrapper.h"
#include "Prefs.h"
#include "Mix.h"

#include <wavpack/wavpack.h>
#include <wx/log.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/stream.h>

#include "../ShuttleGui.h"
#include "../ProjectSettings.h"
#include "../widgets/AudacityMessageBox.h"
#include "../widgets/ProgressDialog.h"
#include "Track.h"
#include "ProjectRate.h"
#include "../Tags.h"

//---------------------------------------------------------------------------
// ExportWavPackOptions
//---------------------------------------------------------------------------

#define ID_HYBRID_MODE 9000

class ExportWavPackOptions final : public wxPanelWrapper
{
public:

   ExportWavPackOptions(wxWindow *parent, int format);
   virtual ~ExportWavPackOptions();

   void PopulateOrExchange(ShuttleGui & S);
   bool TransferDataToWindow() override;
   bool TransferDataFromWindow() override;

   void OnHybridMode(wxCommandEvent& evt);

private:
   wxCheckBox *mCreateCorrectionFile { nullptr };
   wxChoice   *mBitRate;

   DECLARE_EVENT_TABLE()
};

BEGIN_EVENT_TABLE(ExportWavPackOptions, wxPanelWrapper)
   EVT_CHECKBOX(ID_HYBRID_MODE, ExportWavPackOptions::OnHybridMode)
END_EVENT_TABLE()

ExportWavPackOptions::ExportWavPackOptions(wxWindow *parent, int WXUNUSED(format))
: wxPanelWrapper(parent, wxID_ANY)
{
   ShuttleGui S(this, eIsCreatingFromPrefs);
   PopulateOrExchange(S);

   TransferDataToWindow();
}

ExportWavPackOptions::~ExportWavPackOptions()
{
   TransferDataFromWindow();
}

const TranslatableStrings ExportQualityNames{
   XO("Low Quality (Fast)") ,
   XO("High Quality (Slow)") ,
   XO("Very High Quality (Slowest)") ,
};

const std::vector< int > ExportQualityValues{
   0,
   1,
   2,
};

namespace {

const TranslatableStrings ExportBitDepthNames{
   XO("16 bit") ,
   XO("24 bit") ,
   XO("32 bit float ") ,
};

const std::vector< int > ExportBitDepthValues{
   16,
   24,
   32,
};

IntSetting QualitySetting{ L"/FileFormats/WavPackEncodeQuality", 1 };
IntSetting BitrateSetting{ L"/FileFormats/WavPackBitrate", 160 };
IntSetting BitDepthSetting{ L"/FileFormats/WavPackBitDepth", 16 };

BoolSetting HybridModeSetting{ L"/FileFormats/WavPackHybridMode", false };
BoolSetting CreateCorrectionFileSetting{ L"/FileFormats/WavPackCreateCorrectionFile", false };

/* 
Copied from ExportMP2.cpp by
   Joshua Haberman
   Markus Meyer
*/

// i18n-hint kbps abbreviates "thousands of bits per second"
inline TranslatableString n_kbps( int n ) { return XO("%d kbps").Format( n ); }

const TranslatableStrings BitRateNames {
   n_kbps(16),
   n_kbps(24),
   n_kbps(32),
   n_kbps(40),
   n_kbps(48),
   n_kbps(56),
   n_kbps(64),
   n_kbps(80),
   n_kbps(96),
   n_kbps(112),
   n_kbps(128),
   n_kbps(160),
   n_kbps(192),
   n_kbps(224),
   n_kbps(256),
   n_kbps(320),
   n_kbps(384),
};

const std::vector< int > BitRateValues {
   16,
   24,
   32,
   40,
   48,
   56,
   64,
   80,
   96,
   112,
   128,
   160,
   192,
   224,
   256,
   320,
   384,
};

}

void ExportWavPackOptions::PopulateOrExchange(ShuttleGui & S)
{
   bool hybridMode = HybridModeSetting.Read();

   S.StartVerticalLay();
   {
      S.StartHorizontalLay(wxEXPAND);
      {
         S.SetSizerProportion(1);
         S.StartMultiColumn(2, wxCENTER);
         {
            S.TieNumberAsChoice(
               XXO("Quality"),
               QualitySetting,
               ExportQualityNames,
               &ExportQualityValues
            );

            S.TieNumberAsChoice(
               XXO("Bit Depth"),
               BitDepthSetting,
               ExportBitDepthNames,
               &ExportBitDepthValues
            );

            S.Id(ID_HYBRID_MODE).TieCheckBox( XXO("Hybrid Mode"), HybridModeSetting);
            mCreateCorrectionFile = S.Disable(!hybridMode).TieCheckBox( XXO("Create Correction(.wvc) File"), CreateCorrectionFileSetting);

            mBitRate = S.Disable(!hybridMode).TieNumberAsChoice(
               XXO("Bit Rate:"),
               BitrateSetting,
               BitRateNames,
               &BitRateValues
            );
         }
         S.EndMultiColumn();
      }
      S.EndHorizontalLay();
   }
   S.EndVerticalLay();
}

bool ExportWavPackOptions::TransferDataToWindow()
{
   return true;
}

bool ExportWavPackOptions::TransferDataFromWindow()
{
   ShuttleGui S(this, eIsSavingToPrefs);
   PopulateOrExchange(S);

   gPrefs->Flush();

   return true;
}

void ExportWavPackOptions::OnHybridMode(wxCommandEvent&)
{
   const auto hybridMode = HybridModeSetting.Toggle();
   mCreateCorrectionFile->Enable(hybridMode);
   mBitRate->Enable(hybridMode);
};

//---------------------------------------------------------------------------
// ExportWavPack
//---------------------------------------------------------------------------

struct WriteId final
{
   uint32_t bytesWritten {};
   uint32_t firstBlockSize {};
   std::unique_ptr<wxFile> file;
};

class ExportWavPack final : public ExportPlugin
{
public:

   ExportWavPack();

   void OptionsCreate(ShuttleGui &S, int format) override;

   ProgressResult Export(AudacityProject *project,
               std::unique_ptr<ProgressDialog> &pDialog,
               unsigned channels,
               const wxFileNameWrapper &fName,
               bool selectedOnly,
               double t0,
               double t1,
               MixerSpec *mixerSpec = NULL,
               const Tags *metadata = NULL,
               int subformat = 0) override;

   static int WriteBlock(void *id, void *data, int32_t length);
};

ExportWavPack::ExportWavPack()
:  ExportPlugin()
{
   AddFormat();
   SetFormat(wxT("WavPack"),0);
   AddExtension(wxT("wv"),0);
   SetMaxChannels(255,0);
   SetCanMetaData(true,0);
   SetDescription(XO("WavPack Files"),0);
}

ProgressResult ExportWavPack::Export(AudacityProject *project,
                       std::unique_ptr<ProgressDialog> &pDialog,
                       unsigned numChannels,
                       const wxFileNameWrapper &fName,
                       bool selectionOnly,
                       double t0,
                       double t1,
                       MixerSpec *mixerSpec,
                       const Tags *metadata,
                       int WXUNUSED(subformat))
{
   WavpackConfig config = {};
   WriteId outWvFile, outWvcFile;
   outWvFile.file = std::make_unique< wxFile >();

   if (!outWvFile.file->Create(fName.GetFullPath(), true) || !outWvFile.file.get()->IsOpened()) {
      AudacityMessageBox( XO("Unable to open target file for writing") );
      return ProgressResult::Failed;
   }
   
   double rate = ProjectRate::Get( *project ).GetRate();
   const auto &tracks = TrackList::Get( *project );

   int quality = QualitySetting.Read();
   bool hybridMode = HybridModeSetting.Read();
   bool createCorrectionFile = CreateCorrectionFileSetting.Read();
   int bitRate = BitrateSetting.Read();
   int bitDepth = BitDepthSetting.Read();

   sampleFormat format = int16Sample;
   if (bitDepth == 24) {
      format = int24Sample;
   } else if (bitDepth == 32) {
      format = floatSample;
   }

   config.num_channels = numChannels;
   config.sample_rate = rate;
   config.channel_mask = config.num_channels == 1 ? 4 : 3; // Microsoft standard, mono = 4, stereo = 3
   config.bits_per_sample = bitDepth;
   config.bytes_per_sample = bitDepth/8;
   config.float_norm_exp = format == floatSample ? 127 : 0;

   if (quality == 0) {
      config.flags |= CONFIG_FAST_FLAG;
   } else if (quality == 1) {
      config.flags |= CONFIG_HIGH_FLAG;
   } else {
      config.flags |= CONFIG_VERY_HIGH_FLAG;
   }

   if (hybridMode) {
      config.flags |= CONFIG_HYBRID_FLAG;
      config.flags |= CONFIG_BITRATE_KBPS;
      config.bitrate = bitRate;

      if (createCorrectionFile) {
         config.flags |= CONFIG_CREATE_WVC;

         outWvcFile.file = std::make_unique< wxFile >();
         if (!outWvcFile.file->Create(fName.GetFullPath().Append("c"), true)) {
            AudacityMessageBox( XO("Unable to create target file for writing") );
            return ProgressResult::Failed;
         }
      }
   }

   WavpackContext *wpc = WavpackOpenFileOutput(WriteBlock, &outWvFile, createCorrectionFile ? &outWvcFile : nullptr);
   auto closeWavPackContext = finally([wpc]() { WavpackCloseFile(wpc); });

   if (!WavpackSetConfiguration64(wpc, &config, -1, nullptr) || !WavpackPackInit(wpc)) {
      ShowExportErrorDialog( WavpackGetErrorMessage(wpc) );
      return ProgressResult::Failed;
   }

   // Samples to write per run
   constexpr size_t SAMPLES_PER_RUN = 8192u;

   const size_t bufferSize = SAMPLES_PER_RUN * numChannels;
   ArrayOf<int32_t> wavpackBuffer{ bufferSize };
   auto updateResult = ProgressResult::Success;
   {
      auto mixer = CreateMixer(tracks, selectionOnly,
         t0, t1,
         numChannels, SAMPLES_PER_RUN, true,
         rate, format, mixerSpec);

      InitProgress( pDialog, fName,
         selectionOnly
            ? XO("Exporting selected audio as WavPack")
            : XO("Exporting the audio as WavPack") );
      auto &progress = *pDialog;

      while (updateResult == ProgressResult::Success) {
         auto samplesThisRun = mixer->Process(SAMPLES_PER_RUN);

         if (samplesThisRun == 0)
            break;
         
         if (format == int16Sample) {
            const char *mixed = mixer->GetBuffer();
            for (decltype(samplesThisRun) j = 0; j < samplesThisRun; j++) {
               for (size_t i = 0; i < numChannels; i++) {
                  int32_t value = *mixed++ & 0xff;
                  value += *mixed++ << 8;
                  wavpackBuffer[j*numChannels + i] = value;
               }
            }
         } else if (format == int24Sample || (WavpackGetMode(wpc) & MODE_FLOAT) == MODE_FLOAT) {
            const int *mixed = reinterpret_cast<const int*>(mixer->GetBuffer());
            for (decltype(samplesThisRun) j = 0; j < samplesThisRun; j++) {
               for (size_t i = 0; i < numChannels; i++) {
                  wavpackBuffer[j*numChannels + i] = *mixed++;
               }
            }
         } else {
            const float *mixed = reinterpret_cast<const float*>(mixer->GetBuffer());
            for (decltype(samplesThisRun) j = 0; j < samplesThisRun; j++) {
               for (size_t i = 0; i < numChannels; i++) {
                  int64_t intValue = static_cast<int64_t>((*mixed++) * (std::numeric_limits<int32_t>::max()));

                  intValue = std::clamp<int64_t>(
                     intValue,
                     std::numeric_limits<int32_t>::min(),
                     std::numeric_limits<int32_t>::max());

                  wavpackBuffer[j*numChannels + i] = static_cast<int32_t>(intValue);
               }
            }
         }

         if (!WavpackPackSamples(wpc, wavpackBuffer.get(), samplesThisRun)) {
            ShowExportErrorDialog( WavpackGetErrorMessage(wpc) );
            return ProgressResult::Failed;
         }

         if (updateResult == ProgressResult::Success)
            updateResult =
               progress.Update(mixer->MixGetCurrentTime() - t0, t1 - t0);
      }
   }

   if (!WavpackFlushSamples(wpc)) {
      ShowExportErrorDialog( WavpackGetErrorMessage(wpc) );
      return ProgressResult::Failed;
   } else {
      if (metadata == NULL)
         metadata = &Tags::Get( *project );

      wxString n;
      for (const auto &pair : metadata->GetRange()) {
         n = pair.first;
         const auto &v = pair.second;
         if (n == TAG_YEAR) {
            n = wxT("DATE");
         }
         WavpackAppendTagItem(wpc,
                              n.mb_str(wxConvUTF8),
                              v.mb_str(wxConvUTF8),
                              static_cast<int>( v.length() ));
      }

      if (!WavpackWriteTag(wpc)) {
         ShowExportErrorDialog( WavpackGetErrorMessage(wpc) );
         return ProgressResult::Failed;
      }
   }

   if ( !outWvFile.file.get()->Close()
      || ( outWvcFile.file && outWvcFile.file.get() && !outWvcFile.file.get()->Close())) {
      return ProgressResult::Failed;
   }

   // wxFile::Create opens the file with only write access
   // So, need to open the file again with both read and write access
   if (!outWvFile.file->Open(fName.GetFullPath(), wxFile::read_write)) {
      ShowExportErrorDialog( "Unable to update the actual length of the file" );
      return ProgressResult::Failed;
   }

   ArrayOf<int32_t> firstBlockBuffer { outWvFile.firstBlockSize };
   size_t bytesRead = outWvFile.file->Read(firstBlockBuffer.get(), outWvFile.firstBlockSize);

   // Update the first block written with the actual number of samples written
   WavpackUpdateNumSamples(wpc, firstBlockBuffer.get());
   outWvFile.file->Seek(0);
   size_t bytesWritten = outWvFile.file->Write(firstBlockBuffer.get(), outWvFile.firstBlockSize);

   if ( !outWvFile.file.get()->Close() ) {
      return ProgressResult::Failed;
   }

   return updateResult;
}

// Based on the implementation of write_block in dbry/WavPack
// src: https://github.com/dbry/WavPack/blob/master/cli/wavpack.c
int ExportWavPack::WriteBlock(void *id, void *data, int32_t length)
{
    if (id == nullptr || data == nullptr || length == 0)
        return true; // This is considered to be success in wavpack.c reference code

    WriteId *outId = static_cast<WriteId*>(id);

    if (!outId->file)
        // This does not match the wavpack.c but in our case if file is nullptr - 
        // the stream error has occured
        return false; 

   //  if (!outId->file->Write(data, length).IsOk()) {
    if (outId->file->Write(data, length) != length) {
        outId->file.reset();
        return false;
    }

    outId->bytesWritten += length;

    if (outId->firstBlockSize == 0)
        outId->firstBlockSize = length;

    return true;
}

void ExportWavPack::OptionsCreate(ShuttleGui &S, int format)
{
   S.AddWindow( safenew ExportWavPackOptions{ S.GetParent(), format } );
}

static Exporter::RegisteredExportPlugin sRegisteredPlugin{ "WavPack",
   []{ return std::make_unique< ExportWavPack >(); }
};
