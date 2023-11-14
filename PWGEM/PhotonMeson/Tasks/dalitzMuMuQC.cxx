// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.
//
// ========================
//
// This code runs loop over dalitz ee table for dalitz QC.
//    Please write to: daiki.sekihata@cern.ch

#include <array>
#include "TString.h"
#include "THashList.h"
#include "TDirectory.h"
#include "Math/Vector4D.h"
#include "Framework/runDataProcessing.h"
#include "Framework/AnalysisTask.h"
#include "Framework/AnalysisDataModel.h"
#include "Framework/ASoAHelpers.h"

#include "Common/Core/RecoDecay.h"
#include "PWGEM/PhotonMeson/DataModel/gammaTables.h"
#include "PWGEM/PhotonMeson/Core/DalitzEECut.h"
#include "PWGEM/PhotonMeson/Core/CutsLibrary.h"
#include "PWGEM/PhotonMeson/Core/HistogramsLibrary.h"

using namespace o2;
using namespace o2::aod;
using namespace o2::framework;
using namespace o2::framework::expressions;
using namespace o2::soa;
using std::array;

using MyCollisions = soa::Join<aod::EMReducedEvents, aod::EMReducedEventsMult, aod::EMReducedEventsCent, aod::EMReducedEventsNgPCM, aod::EMReducedEventsNgPHOS, aod::EMReducedEventsNgEMC>;
using MyCollision = MyCollisions::iterator;

using MyDalitzMuMus = soa::Join<aod::DalitzMuMus, aod::DalitzMuMuEMReducedEventIds>;
using MyDalitzMuMu = MyDalitzMuMus::iterator;

using MyTracks = soa::Join<aod::EMPrimaryMuons, aod::EMPrimaryMuonEMReducedEventIds>;
using MyTrack = MyTracks::iterator;

struct DalitzMuMuQC {
  Configurable<std::string> fConfigDalitzMuMuCuts{"cfgDalitzMuMuCuts", "nocut", "Comma separated list of dalitz mumu cuts"};
  std::vector<DalitzEECut> fDalitzMuMuCuts;

  OutputObj<THashList> fOutputEvent{"Event"};
  OutputObj<THashList> fOutputTrack{"Track"};
  OutputObj<THashList> fOutputDalitzMuMu{"DalitzMuMu"};
  THashList* fMainList = new THashList();

  void addhistograms()
  {
    fMainList->SetOwner(true);
    fMainList->SetName("fMainList");

    // create sub lists first.
    o2::aod::emphotonhistograms::AddHistClass(fMainList, "Event");
    THashList* list_ev = reinterpret_cast<THashList*>(fMainList->FindObject("Event"));
    o2::aod::emphotonhistograms::DefineHistograms(list_ev, "Event");

    o2::aod::emphotonhistograms::AddHistClass(fMainList, "Track");
    THashList* list_track = reinterpret_cast<THashList*>(fMainList->FindObject("Track"));

    o2::aod::emphotonhistograms::AddHistClass(fMainList, "DalitzMuMu");
    THashList* list_dalitzmumu = reinterpret_cast<THashList*>(fMainList->FindObject("DalitzMuMu"));

    for (const auto& cut : fDalitzMuMuCuts) {
      const char* cutname = cut.GetName();
      o2::aod::emphotonhistograms::AddHistClass(list_track, cutname);
      o2::aod::emphotonhistograms::AddHistClass(list_dalitzmumu, cutname);
    }

    // for single tracks
    for (auto& cut : fDalitzMuMuCuts) {
      std::string_view cutname = cut.GetName();
      THashList* list = reinterpret_cast<THashList*>(fMainList->FindObject("Track")->FindObject(cutname.data()));
      o2::aod::emphotonhistograms::DefineHistograms(list, "Track");
    }

    // for DalitzMuMus
    for (auto& cut : fDalitzMuMuCuts) {
      std::string_view cutname = cut.GetName();
      THashList* list = reinterpret_cast<THashList*>(fMainList->FindObject("DalitzMuMu")->FindObject(cutname.data()));
      o2::aod::emphotonhistograms::DefineHistograms(list, "DalitzMuMu");
    }
  }

  void DefineCuts()
  {
    TString cutNamesStr = fConfigDalitzMuMuCuts.value;
    if (!cutNamesStr.IsNull()) {
      std::unique_ptr<TObjArray> objArray(cutNamesStr.Tokenize(","));
      for (int icut = 0; icut < objArray->GetEntries(); ++icut) {
        const char* cutname = objArray->At(icut)->GetName();
        LOGF(info, "add cut : %s", cutname);
        fDalitzMuMuCuts.push_back(*dalitzeecuts::GetCut(cutname));
      }
    }
    LOGF(info, "Number of Dalitz cuts = %d", fDalitzMuMuCuts.size());
  }

  void init(InitContext& context)
  {
    DefineCuts();
    addhistograms(); // please call this after DefinCuts();

    fOutputEvent.setObject(reinterpret_cast<THashList*>(fMainList->FindObject("Event")));
    fOutputTrack.setObject(reinterpret_cast<THashList*>(fMainList->FindObject("Track")));
    fOutputDalitzMuMu.setObject(reinterpret_cast<THashList*>(fMainList->FindObject("DalitzMuMu")));
  }

  Partition<MyDalitzMuMus> uls_pairs = o2::aod::dalitzmumu::sign == 0;
  Partition<MyDalitzMuMus> lspp_pairs = o2::aod::dalitzmumu::sign == +1;
  Partition<MyDalitzMuMus> lsmm_pairs = o2::aod::dalitzmumu::sign == -1;

  SliceCache cache;
  Preslice<MyDalitzMuMus> perCollision = aod::dalitzmumu::emreducedeventId;

  std::vector<uint64_t> used_trackIds;

  void processQC(MyCollisions const& collisions, MyDalitzMuMus const& dileptons, MyTracks const& tracks)
  {
    THashList* list_ev = static_cast<THashList*>(fMainList->FindObject("Event"));
    THashList* list_dalitzmumu = static_cast<THashList*>(fMainList->FindObject("DalitzMuMu"));
    THashList* list_track = static_cast<THashList*>(fMainList->FindObject("Track"));
    double values[4] = {0, 0, 0, 0};

    for (auto& collision : collisions) {
      reinterpret_cast<TH1F*>(fMainList->FindObject("Event")->FindObject("hZvtx_before"))->Fill(collision.posZ());
      reinterpret_cast<TH1F*>(fMainList->FindObject("Event")->FindObject("hCollisionCounter"))->Fill(1.0);
      if (!collision.sel8()) {
        continue;
      }
      reinterpret_cast<TH1F*>(fMainList->FindObject("Event")->FindObject("hCollisionCounter"))->Fill(2.0);

      if (collision.numContrib() < 0.5) {
        continue;
      }
      reinterpret_cast<TH1F*>(fMainList->FindObject("Event")->FindObject("hCollisionCounter"))->Fill(3.0);

      if (abs(collision.posZ()) > 10.0) {
        continue;
      }
      reinterpret_cast<TH1F*>(fMainList->FindObject("Event")->FindObject("hCollisionCounter"))->Fill(4.0);
      reinterpret_cast<TH1F*>(fMainList->FindObject("Event")->FindObject("hZvtx_after"))->Fill(collision.posZ());
      o2::aod::emphotonhistograms::FillHistClass<EMHistType::kEvent>(list_ev, "", collision);

      auto uls_pairs_per_coll = uls_pairs->sliceByCached(o2::aod::dalitzmumu::emreducedeventId, collision.globalIndex(), cache);
      auto lspp_pairs_per_coll = lspp_pairs->sliceByCached(o2::aod::dalitzmumu::emreducedeventId, collision.globalIndex(), cache);
      auto lsmm_pairs_per_coll = lsmm_pairs->sliceByCached(o2::aod::dalitzmumu::emreducedeventId, collision.globalIndex(), cache);

      for (const auto& cut : fDalitzMuMuCuts) {
        THashList* list_dalitzmumu_cut = static_cast<THashList*>(list_dalitzmumu->FindObject(cut.GetName()));
        THashList* list_track_cut = static_cast<THashList*>(list_track->FindObject(cut.GetName()));
        used_trackIds.reserve(uls_pairs_per_coll.size() * 2);

        int nuls = 0, nlspp = 0, nlsmm = 0;
        for (auto& uls_pair : uls_pairs_per_coll) {
          auto pos = uls_pair.template posTrack_as<MyTracks>();
          auto ele = uls_pair.template negTrack_as<MyTracks>();
          if (cut.IsSelected<MyTracks>(uls_pair)) {
            values[0] = uls_pair.mass();
            values[1] = uls_pair.pt();
            values[2] = uls_pair.dcaXY();
            values[3] = uls_pair.phiv();
            reinterpret_cast<THnSparseF*>(list_dalitzmumu_cut->FindObject("hs_dilepton_uls"))->Fill(values);
            nuls++;
            for (auto& track : {pos, ele}) {
              if (std::find(used_trackIds.begin(), used_trackIds.end(), track.globalIndex()) == used_trackIds.end()) {
                o2::aod::emphotonhistograms::FillHistClass<EMHistType::kTrack>(list_track_cut, "", track);
                used_trackIds.emplace_back(track.globalIndex());
              }
            }
          }
        } // end of uls pair loop
        reinterpret_cast<TH1F*>(list_dalitzmumu_cut->FindObject("hNpair_uls"))->Fill(nuls);

        for (auto& lspp_pair : lspp_pairs_per_coll) {
          if (cut.IsSelected<MyTracks>(lspp_pair)) {
            values[0] = lspp_pair.mass();
            values[1] = lspp_pair.pt();
            values[2] = lspp_pair.dcaXY();
            values[3] = lspp_pair.phiv();
            reinterpret_cast<THnSparseF*>(list_dalitzmumu_cut->FindObject("hs_dilepton_lspp"))->Fill(values);
            nlspp++;
          }
        } // end of lspp pair loop
        reinterpret_cast<TH1F*>(list_dalitzmumu_cut->FindObject("hNpair_lspp"))->Fill(nlspp);

        for (auto& lsmm_pair : lsmm_pairs_per_coll) {
          if (cut.IsSelected<MyTracks>(lsmm_pair)) {
            values[0] = lsmm_pair.mass();
            values[1] = lsmm_pair.pt();
            values[2] = lsmm_pair.dcaXY();
            values[3] = lsmm_pair.phiv();
            reinterpret_cast<THnSparseF*>(list_dalitzmumu_cut->FindObject("hs_dilepton_lsmm"))->Fill(values);
            nlsmm++;
          }
        } // end of lsmm pair loop
        reinterpret_cast<TH1F*>(list_dalitzmumu_cut->FindObject("hNpair_lsmm"))->Fill(nlsmm);

        used_trackIds.clear();
        used_trackIds.shrink_to_fit();
      } // end of cut loop
    }   // end of collision loop
  }     // end of process
  PROCESS_SWITCH(DalitzMuMuQC, processQC, "run Dalitz QC", true);

  void processDummy(MyCollisions const& collisions) {}
  PROCESS_SWITCH(DalitzMuMuQC, processDummy, "Dummy function", false);
};

WorkflowSpec defineDataProcessing(ConfigContext const& cfgc)
{
  return WorkflowSpec{
    adaptAnalysisTask<DalitzMuMuQC>(cfgc, TaskName{"dalitz-mumu-qc"})};
}