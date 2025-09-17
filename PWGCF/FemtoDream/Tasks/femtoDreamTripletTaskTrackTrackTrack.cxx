// Copyright 2019-2025 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

/// \file femtoDreamTripletTaskTrackTrackTrack.cxx
/// \brief Tasks that reads the track tables and creates track triplets; only three identical particles can be used
/// \author Laura Serksnyte, TU München, laura.serksnyte@tum.de

#include <vector>
#include <string>
#include "Framework/AnalysisTask.h"
#include "Framework/runDataProcessing.h"
#include "Framework/HistogramRegistry.h"
#include "Framework/ASoAHelpers.h"
#include "Framework/RunningWorkflowInfo.h"
#include "Framework/StepTHn.h"
#include "Framework/O2DatabasePDGPlugin.h"
#include "TDatabasePDG.h"
#include <algorithm>

#include "PWGCF/DataModel/FemtoDerived.h"
#include "PWGCF/FemtoDream/Core/femtoDreamParticleHisto.h"
#include "PWGCF/FemtoDream/Core/femtoDreamEventHisto.h"
#include "PWGCF/FemtoDream/Core/femtoDreamPairCleaner.h"
#include "PWGCF/FemtoDream/Core/femtoDreamContainerThreeBody.h"
#include "PWGCF/FemtoDream/Core/femtoDreamDetaDphiStar.h"
#include "PWGCF/FemtoDream/Core/femtoDreamUtils.h"

using namespace o2;
using namespace o2::analysis::femtoDream;
using namespace o2::framework;
using namespace o2::framework::expressions;
using namespace o2::soa;

struct femtoDreamTripletTaskTrackTrackTrack {
  SliceCache cache;
  Preslice<aod::FDParticles> perCol = aod::femtodreamparticle::fdCollisionId;


  Configurable<float> confSphericityMin{"confSphericityMin", 0.6, "Minimum Sphericity Percentile"};
  Configurable<float> confSphericityMax{"confSphericityMax", 1, "Maximum Sphericity Percentile"};

  Filter EventSphericity = aod::femtodreamcollision::sphericity >= confSphericityMin && aod::femtodreamcollision::sphericity <= confSphericityMax;
  using FilteredFDCollisions = soa::Filtered<aod::FDCollisions>;
  using FilteredFDCollision = FilteredFDCollisions::iterator;

  using MaskedCollisions =  soa::Filtered<soa::Join<aod::FDCollisions, aod::FDColMasks>>;
  using MaskedCollision = MaskedCollisions::iterator;


  using MCCollisions = soa::Filtered<soa::Join<aod::FDCollisions, aod::FDMCCollLabels>>;
  using MCCollision = MCCollisions::iterator;


  using MaskedMCCollisions = soa::Filtered<soa::Join<aod::FDCollisions, aod::FDMCCollLabels, aod::FDColMasks>>;
  using MaskedMCCollision = MaskedMCCollisions::iterator;

  aod::femtodreamcollision::BitMaskType MaskBit = -1;
  float mMassOne = -999, mMassTwo = -999, mMassThree = -999;

  /// Particle selection part

  // which CPR to use, old is with a possible bug and new is fixed
  Configurable<bool> ConfUseOLD_possiblyWrong_CPR{"ConfUseOLD_possiblyWrong_CPR", true, "Use for old CPR, which possibly has a bug. This is implemented only for debugging reasons to compare old and new code on hyperloop datasets."};

  /// Table for both particles
  Configurable<float> ConfTracksInMixedEvent{"ConfTracksInMixedEvent", 1, "Number of tracks of interest, contained in the mixed event sample: 1 - only events with at least one track of interest are used in mixing; ...; 3 - only events with at least three track of interest are used in mixing. Max value is 3"};
  Configurable<float> ConfMaxpT{"ConfMaxpT", 4.05f, "Maximum transverse momentum of the particles"};
  Configurable<float> ConfMinpT{"ConfMinpT", 0.3f, "Minimum transverse momentum of the particles"};
  Configurable<float> ConfRejectEtaAt0{"ConfRejectEtaAt0", 0.02f, "Cut out central eta events"};
  Configurable<float> ConfMaxDCAxy{"ConfMaxDCAxy", -0.1f, "Maximum DCAxy of the particles"};
  Configurable<float> ConfMinDCAxy{"ConfMinDCAxy", 0.1f, "Minimum DCAxy of the particles"};
  Configurable<float> ConfPIDthrMom{"ConfPIDthrMom", 1.f, "Momentum threshold from which TPC and TOF are required for PID"};
  Configurable<int> ConfAtWhichRadiiToCut{"ConfAtWhichRadiiToCut", 1, "At which radii perform deta dphi selection: 0 - at PV, 1 - averaged phi, 2 - at given radii"};
  Configurable<float> ConfAtWhichTPCRadii{"ConfAtWhichTPCRadii", 85., "If ConfAtWhichRadiiToCut = 2; this allows to select at which TPC radii to cut"};
  Configurable<o2::aod::femtodreamparticle::cutContainerType> ConfTPCPIDBit{"ConfTPCPIDBit", 16, "PID TPC bit from cutCulator "};
  Configurable<o2::aod::femtodreamparticle::cutContainerType> ConfTPCTOFPIDBit{"ConfTPCTOFPIDBit", 8, "PID TPCTOF bit from cutCulator"};
  Configurable<bool> ConfIsMC{"ConfIsMC", false, "Enable additional Histogramms in the case of a MonteCarlo Run"};
  Configurable<bool> ConfUse3D{"ConfUse3D", false, "Enable three dimensional histogramms (to be used only for analysis with high statistics): k* vs mT vs multiplicity"};
  Configurable<bool> ConfDCACutPtDep{"ConfDCACutPtDep", false, "Use pt dependent dca cut for tracks"};




  
  // Which particles to analyse; currently support only for same species and cuts triplets
  Configurable<int> ConfPDGCodePart{"ConfPDGCodePart", 2212, "Particle PDG code"};
  Configurable<o2::aod::femtodreamparticle::cutContainerType> ConfCutPart{"ConfCutPart", 5542474, "Track - Selection bit from cutCulator"};

  /// Partition for selected particles
  Partition<aod::FDParticles> SelectedParts = /*((aod::femtodreamparticle::phi > float(0.369) && aod::femtodreamparticle::phi < float(0.678)) ||
                                              (aod::femtodreamparticle::phi > float(0.718) && aod::femtodreamparticle::phi < float(1.027)) ||
                                              (aod::femtodreamparticle::phi > float(1.067) && aod::femtodreamparticle::phi < float(1.376)) ||
                                              (aod::femtodreamparticle::phi > float(1.416) && aod::femtodreamparticle::phi < float(1.725)) ||
                                              (aod::femtodreamparticle::phi > float(1.765) && aod::femtodreamparticle::phi < float(2.074)) ||
                                              (aod::femtodreamparticle::phi > float(2.114) && aod::femtodreamparticle::phi < float(2.423)) ||
                                              (aod::femtodreamparticle::phi > float(2.463) && aod::femtodreamparticle::phi < float(2.772)) ||
                                              (aod::femtodreamparticle::phi > float(2.812) && aod::femtodreamparticle::phi < float(3.122)) ||
                                              (aod::femtodreamparticle::phi > float(3.162) && aod::femtodreamparticle::phi < float(3.471)) ||
                                              (aod::femtodreamparticle::phi > float(3.511) && aod::femtodreamparticle::phi < float(3.820)) ||
                                              (aod::femtodreamparticle::phi > float(3.860) && aod::femtodreamparticle::phi < float(4.169)) ||
                                              (aod::femtodreamparticle::phi > float(4.209) && aod::femtodreamparticle::phi < float(4.518)) ||
                                              (aod::femtodreamparticle::phi > float(4.558) && aod::femtodreamparticle::phi < float(4.867)) ||
                                              (aod::femtodreamparticle::phi > float(4.907) && aod::femtodreamparticle::phi < float(5.216)) ||
                                              (aod::femtodreamparticle::phi > float(5.256) && aod::femtodreamparticle::phi < float(5.565)) ||
                                              (aod::femtodreamparticle::phi > float(5.605) && aod::femtodreamparticle::phi < float(5.914)) ||
                                              (aod::femtodreamparticle::phi > float(5.954) && aod::femtodreamparticle::phi < float(6.263)) ||
                                              (aod::femtodreamparticle::phi > float(0.02)   && aod::femtodreamparticle::phi < float(0.329)))&&*/
                                              //((aod::femtodreamparticle::eta > ConfRejectEtaAt0) || (aod::femtodreamparticle::eta < -ConfRejectEtaAt0)) &&
                                              (aod::femtodreamparticle::partType == uint8_t(aod::femtodreamparticle::ParticleType::kTrack)) &&
                                              ifnode(aod::femtodreamparticle::pt * (nexp(aod::femtodreamparticle::eta) + nexp(-1.f * aod::femtodreamparticle::eta)) / 2.f <= ConfPIDthrMom, ncheckbit(aod::femtodreamparticle::pidcut, ConfTPCPIDBit), ncheckbit(aod::femtodreamparticle::pidcut, ConfTPCTOFPIDBit)) &&
                                              (ncheckbit(aod::femtodreamparticle::cut, ConfCutPart)) &&
                                              (aod::femtodreamparticle::pt < ConfMaxpT) &&
                                              (aod::femtodreamparticle::pt > ConfMinpT) &&
                                              ifnode(ConfDCACutPtDep, (nabs(aod::femtodreamparticle::tempFitVar) <= 0.004f + 0.013f / aod::femtodreamparticle::pt),
                                                     ((aod::femtodreamparticle::tempFitVar >= ConfMinDCAxy) &&
                                                      (aod::femtodreamparticle::tempFitVar <= ConfMaxDCAxy)));
  ;

  Partition<soa::Join<aod::FDParticles, aod::FDMCLabels>> SelectedPartsMC = (aod::femtodreamparticle::partType == uint8_t(aod::femtodreamparticle::ParticleType::kTrack)) &&
                                                                            ifnode(aod::femtodreamparticle::pt * (nexp(aod::femtodreamparticle::eta) + nexp(-1.f * aod::femtodreamparticle::eta)) / 2.f <= ConfPIDthrMom, ncheckbit(aod::femtodreamparticle::pidcut, ConfTPCPIDBit), ncheckbit(aod::femtodreamparticle::pidcut, ConfTPCTOFPIDBit)) &&
                                                                            (ncheckbit(aod::femtodreamparticle::cut, ConfCutPart)) &&
                                                                            (aod::femtodreamparticle::pt < ConfMaxpT) &&
                                                                            (aod::femtodreamparticle::pt > ConfMinpT) &&
                                                                            ifnode(ConfDCACutPtDep, (nabs(aod::femtodreamparticle::tempFitVar) <= 0.004f + 0.013f / aod::femtodreamparticle::pt),
                                                                                   ((aod::femtodreamparticle::tempFitVar >= ConfMinDCAxy) &&
                                                                                    (aod::femtodreamparticle::tempFitVar <= ConfMaxDCAxy)));
  ;

  /// Histogramming of Selected Particles
  FemtoDreamParticleHisto<aod::femtodreamparticle::ParticleType::kTrack, 1> trackHistoSelectedParts;
  FemtoDreamParticleHisto<aod::femtodreamparticle::ParticleType::kTrack, 5> trackHistoALLSelectedParts;

  /// Histogramming for Event
  FemtoDreamEventHisto eventHisto;

  /// particle part
  ConfigurableAxis ConfTempFitVarBins{"ConfTempFitVarBins", {300, -0.15, 0.15}, "binning of the TempFitVar in the pT vs. TempFitVar plot"};
  ConfigurableAxis ConfTempFitVarpTBins{"ConfTempFitVarpTBins", {20, 0.5, 4.05}, "pT binning of the pT vs. TempFitVar plot"};
  ConfigurableAxis ConfBinmultTempFit{"ConfBinmultTempFit", {1, 0, 1}, "multiplicity Binning for the TempFitVar plot"};

  /// Correlation part
  ConfigurableAxis ConfMultBins{"ConfMultBins", {VARIABLE_WIDTH, 0.0f, 20.0f, 40.0f, 60.0f, 80.0f, 100.0f, 200.0f, 99999.f}, "Mixing bins - multiplicity"};
  ConfigurableAxis ConfVtxBins{"ConfVtxBins", {VARIABLE_WIDTH, -10.0f, -8.f, -6.f, -4.f, -2.f, 0.f, 2.f, 4.f, 6.f, 8.f, 10.f}, "Mixing bins - z-vertex"};
  
  Configurable<float> ConfMaxQ3ForkStarPlots{"ConfMaxQ3ForkStarPlots", 0.4, "ConfMaxQ3ForkStarPlots - Maximum Q3 for k* vs k* plots"};
  
  ColumnBinningPolicy<aod::collision::PosZ, aod::femtodreamcollision::MultNtr> colBinning{{ConfVtxBins, ConfMultBins}, true};

  ConfigurableAxis ConfQ3Bins{"ConfQ3Bins", {2000, 0., 8.}, "binning Q3"};
  ConfigurableAxis ConfQ3BinsFor4D{"ConfQ3BinsFor4D", {500, 0., 2.}, "binning Q3 for 4D hist"};
  Configurable<int> ConfNEventsMix{"ConfNEventsMix", 5, "Number of events for mixing"};
  Configurable<bool> ConfIsCPR{"ConfIsCPR", true, "Close Pair Rejection"};
  Configurable<bool> ConfFillCPRQA{"ConfFillCPRQA", false, "Fill Close Pair Rejection plots as a function of eta and phi"};
  Configurable<bool> ConfCPRPlotPerRadii{"ConfCPRPlotPerRadii", false, "Plot CPR per radii"};
  Configurable<float> ConfCPRdeltaPhiMax{"ConfCPRdeltaPhiMax", 0.01, "Max. Delta Phi for Close Pair Rejection"};
  Configurable<float> ConfCPRdeltaEtaMax{"ConfCPRdeltaEtaMax", 0.01, "Max. Delta Eta for Close Pair Rejection"};
  Configurable<float> ConfMaxQ3IncludedInCPRPlots{"ConfMaxQ3IncludedInCPRPlots", 8., "Maximum Q3, for which the pair CPR is included in plots"};
  ConfigurableAxis ConfDummy{"ConfDummy", {1, 0, 1}, "Dummy axis"};

  FemtoDreamContainerThreeBody<femtoDreamContainerThreeBody::EventType::same, femtoDreamContainerThreeBody::Observable::Q3> sameEventCont;
  FemtoDreamContainerThreeBody<femtoDreamContainerThreeBody::EventType::mixed, femtoDreamContainerThreeBody::Observable::Q3> mixedEventCont;
  FemtoDreamPairCleaner<aod::femtodreamparticle::ParticleType::kTrack, aod::femtodreamparticle::ParticleType::kTrack> pairCleaner;
  FemtoDreamDetaDphiStar<aod::femtodreamparticle::ParticleType::kTrack, aod::femtodreamparticle::ParticleType::kTrack> pairCloseRejectionSE;
  FemtoDreamDetaDphiStar<aod::femtodreamparticle::ParticleType::kTrack, aod::femtodreamparticle::ParticleType::kTrack> pairCloseRejectionME;
  /// Histogram output
  HistogramRegistry qaRegistry{"TrackQA", {}, OutputObjHandlingPolicy::AnalysisObject};
  HistogramRegistry resultRegistry{"Correlations", {}, OutputObjHandlingPolicy::AnalysisObject};
  HistogramRegistry ThreeBodyQARegistry{"ThreeBodyQARegistry", {}, OutputObjHandlingPolicy::AnalysisObject};

  void init(InitContext& context)
  {

    eventHisto.init(&qaRegistry, false);

    colBinning = {{ConfVtxBins, ConfMultBins}, true};

    trackHistoSelectedParts.init(&qaRegistry, ConfBinmultTempFit, ConfDummy, ConfTempFitVarpTBins, ConfDummy, ConfDummy, ConfTempFitVarBins, ConfDummy, ConfDummy, ConfDummy, ConfDummy, ConfDummy, ConfDummy, ConfIsMC, ConfPDGCodePart);
    trackHistoALLSelectedParts.init(&qaRegistry, ConfBinmultTempFit, ConfDummy, ConfTempFitVarpTBins, ConfDummy, ConfDummy, ConfTempFitVarBins, ConfDummy, ConfDummy, ConfDummy, ConfDummy, ConfDummy, ConfDummy, ConfIsMC, ConfPDGCodePart);

    ThreeBodyQARegistry.add("TripletTaskQA/hSECollisionBins", ";bin;Entries", kTH1F, {{120, -0.5, 119.5}});
    ThreeBodyQARegistry.add("TripletTaskQA/hMECollisionBins", ";bin;Entries", kTH1F, {{120, -0.5, 119.5}});
    ThreeBodyQARegistry.add("TripletTaskQA/particle_pT_in_Triplet_SE", "; p_{T1} ; p_{T2} ; p_{T3} ; Q_{3}", kTHnSparseF, {ConfTempFitVarpTBins, ConfTempFitVarpTBins, ConfTempFitVarpTBins, ConfQ3BinsFor4D});
    ThreeBodyQARegistry.add("TripletTaskQA/particle_pT_in_Triplet_ME", "; p_{T1} ; p_{T2} ; p_{T3} ; Q_{3}", kTHnSparseF, {ConfTempFitVarpTBins, ConfTempFitVarpTBins, ConfTempFitVarpTBins, ConfQ3BinsFor4D});
    std::vector<double> tmpVecMult = ConfMultBins;
    framework::AxisSpec multAxis = {tmpVecMult, "Multiplicity"};
    ThreeBodyQARegistry.add("TripletTaskQA/hSEMultVSGoodTracks", ";Mult;GoodT", kTH2F, {multAxis, {100, 0, 100}});
    ThreeBodyQARegistry.add("TripletTaskQA/hTripletsPerEventBelow14", ";Triplets;Entries", kTH1F, {{10, 0, 10}});
    ThreeBodyQARegistry.add("TripletTaskQA/NumberOfTacksPassingSelection", ";Triplets;Entries", kTH1F, {{30, 0, 30}});
    if (ConfIsMC) {
      ThreeBodyQARegistry.add("TrackMC_QA/hMazzachi", ";gen;(reco-gen)/gen", kTH2F, {{100, ConfMinpT, ConfMaxpT}, {300, -1, 1}});
    }
    ThreeBodyQARegistry.add("TripletTaskQA/hCentrality", ";Centrality; Q3", kTH2F, {{100, 0, 100}, ConfQ3Bins});
    ThreeBodyQARegistry.add("TripletTaskQA/kstartkstarLargestLargest", ";k*; k*", kTH2F, {{400, 0, 4}, {400, 0, 4}});
    ThreeBodyQARegistry.add("TripletTaskQA/kstartkstarLargestSmallest", ";k*; k*", kTH2F, {{400, 0, 4}, {400, 0, 4}});
    ThreeBodyQARegistry.add("TripletTaskQA/kstartkstarLargestLargestME", ";k*; k*", kTH2F, {{400, 0, 4}, {400, 0, 4}});
    ThreeBodyQARegistry.add("TripletTaskQA/kstartkstarLargestSmallestME", ";k*; k*", kTH2F, {{400, 0, 4}, {400, 0, 4}});


    ThreeBodyQARegistry.add("TripletTaskQA/phiVSdPhiSE", ";phi; dphi ", kTH2F, {{200, -6.4, 6.4}, {200, -6.4, 6.4}});
    ThreeBodyQARegistry.add("TripletTaskQA/phiVSdPhiME", ";phi; dphi", kTH2F, {{200, -6.4, 6.4}, {200, -6.4, 6.4}});


    ThreeBodyQARegistry.add("TripletTaskQA/phiBelowQ3", ";phi", kTH1F, {{200, -6.4, 6.4}});

    sameEventCont.init(&resultRegistry, ConfQ3Bins, ConfMultBins, ConfIsMC);
    mixedEventCont.init(&resultRegistry, ConfQ3Bins, ConfMultBins, ConfIsMC);
    sameEventCont.setPDGCodes(ConfPDGCodePart, ConfPDGCodePart, ConfPDGCodePart);
    mixedEventCont.setPDGCodes(ConfPDGCodePart, ConfPDGCodePart, ConfPDGCodePart);
    pairCleaner.init(&qaRegistry); // SERKSNYTE : later check if init should be updated to have 3 separate histos
    if (ConfIsCPR.value) {
      pairCloseRejectionSE.init(&resultRegistry, &qaRegistry, ConfCPRdeltaPhiMax.value, ConfCPRdeltaEtaMax.value, ConfCPRPlotPerRadii.value, 1, ConfUseOLD_possiblyWrong_CPR, ConfMaxQ3IncludedInCPRPlots, false, ConfAtWhichRadiiToCut, ConfAtWhichTPCRadii, ConfFillCPRQA);
      pairCloseRejectionME.init(&resultRegistry, &qaRegistry, ConfCPRdeltaPhiMax.value, ConfCPRdeltaEtaMax.value, ConfCPRPlotPerRadii.value, 2, ConfUseOLD_possiblyWrong_CPR, ConfMaxQ3IncludedInCPRPlots, false, ConfAtWhichRadiiToCut, ConfAtWhichTPCRadii, ConfFillCPRQA);
    }

    // get masses
    mMassOne = TDatabasePDG::Instance()->GetParticle(ConfPDGCodePart)->Mass();
    mMassTwo = TDatabasePDG::Instance()->GetParticle(ConfPDGCodePart)->Mass();
    mMassThree = TDatabasePDG::Instance()->GetParticle(ConfPDGCodePart)->Mass();

    // get bit for the collision mask
    std::bitset<8 * sizeof(aod::femtodreamcollision::BitMaskType)> mask;
    int index = 0;
    auto& workflows = context.services().get<RunningWorkflowInfo const>();
    for (DeviceSpec const& device : workflows.devices) {
      if (device.name.find("femto-dream-triplet-task-track-track-track") != std::string::npos) {
        if (containsNameValuePair(device.options, "ConfCutPart", ConfCutPart.value) &&
            containsNameValuePair(device.options, "ConfTPCPIDBit", ConfTPCPIDBit.value) &&
            containsNameValuePair(device.options, "ConfTPCTOFPIDBit", ConfTPCTOFPIDBit.value) &&
            containsNameValuePair(device.options, "ConfPIDthrMom", ConfPIDthrMom.value) &&
            containsNameValuePair(device.options, "ConfMaxpT", ConfMaxpT.value) &&
            containsNameValuePair(device.options, "ConfMinpT", ConfMinpT.value) &&
            containsNameValuePair(device.options, "ConfMaxDCAxy", ConfMaxDCAxy.value) &&
            containsNameValuePair(device.options, "ConfMinDCAxy", ConfMinDCAxy.value)) {
          mask.set(index);
          MaskBit = static_cast<aod::femtodreamcollision::BitMaskType>(mask.to_ulong());
          LOG(info) << "Device name matched: " << device.name;
          LOG(info) << "Bitmask for collisions: " << mask.to_string();
          break;
        } else {
          index++;
        }
      }
    }

    if ((doprocessSameEvent && doprocessSameEventMasked) ||
        (doprocessMixedEvent && doprocessMixedEventMasked) ||
        (doprocessSameEventMC && doprocessSameEventMCMasked) ||
        (doprocessMixedEventMC && doprocessMixedEventMCMasked)) {
      LOG(fatal) << "Normal and masked processing cannot be activated simultaneously!";
    }
  }

  template <bool isMC, typename CollisionType>
  void fillCollision(CollisionType col)
  {
    ThreeBodyQARegistry.fill(HIST("TripletTaskQA/hSECollisionBins"), colBinning.getBin({col.posZ(), col.multNtr()}));
    eventHisto.fillQA<isMC>(col);
  }

  /// This function processes the same event and takes care of all the histogramming
  /// @tparam PartitionType
  /// @tparam PartType
  /// @tparam isMC: enables Monte Carlo truth specific histograms
  /// @param groupSelectedParts partition for the first particle passed by the process function
  /// @param parts femtoDreamParticles table (in case of Monte Carlo joined with FemtoDreamMCLabels)
  /// @param magFieldTesla magnetic field of the collision
  /// @param multCol multiplicity of the collision
  template <bool isMC, typename PartitionType, typename PartType>
  void doSameEvent(PartitionType groupSelectedParts, PartType parts, float magFieldTesla, int multCol, float centCol)
  {
    /// Histogramming same event
    int numberOfTracksPassingSelection = 0;
    for (auto& part : groupSelectedParts) {
      numberOfTracksPassingSelection = numberOfTracksPassingSelection + 1;
      trackHistoSelectedParts.fillQA<isMC, false>(part, aod::femtodreamparticle::kPt, multCol, centCol);
    }
    ThreeBodyQARegistry.fill(HIST("TripletTaskQA/NumberOfTacksPassingSelection"), numberOfTracksPassingSelection);

    /// Now build the combinations
    int numberOfTriplets = 0;
    for (auto& [p1, p2, p3] : combinations(CombinationsStrictlyUpperIndexPolicy(groupSelectedParts, groupSelectedParts, groupSelectedParts))) {
      auto Q3 = FemtoDreamMath::getQ3(p1, mMassOne, p2, mMassTwo, p3, mMassThree);

      if (ConfIsCPR.value) {
        if (pairCloseRejectionSE.isClosePair(p1, p2, parts, magFieldTesla, Q3)) {
          continue;
        }
        if (pairCloseRejectionSE.isClosePair(p2, p3, parts, magFieldTesla, Q3)) {
          continue;
        }
        if (pairCloseRejectionSE.isClosePair(p1, p3, parts, magFieldTesla, Q3)) {
          continue;
        }
      }

      // track cleaning
      if (!pairCleaner.isCleanPair(p1, p2, parts)) {
        continue;
      }
      if (!pairCleaner.isCleanPair(p2, p3, parts)) {
        continue;
      }
      if (!pairCleaner.isCleanPair(p1, p3, parts)) {
        continue;
      }

      ThreeBodyQARegistry.fill(HIST("TripletTaskQA/phiVSdPhiSE"), p1.phi(), p1.phi()-p2.phi()); 
      ThreeBodyQARegistry.fill(HIST("TripletTaskQA/phiVSdPhiSE"), p2.phi(), p2.phi()-p3.phi()); 
      ThreeBodyQARegistry.fill(HIST("TripletTaskQA/phiVSdPhiSE"), p3.phi(), p3.phi()-p1.phi()); 

      // fill pT of all three particles as a function of Q3 for lambda calculations
      if (Q3 < 1.4) {
        numberOfTriplets = numberOfTriplets + 1;
      }

      if (Q3 < 0.8){
        ThreeBodyQARegistry.fill(HIST("TripletTaskQA/phiBelowQ3"), p1.phi());
        ThreeBodyQARegistry.fill(HIST("TripletTaskQA/phiBelowQ3"), p2.phi());
        ThreeBodyQARegistry.fill(HIST("TripletTaskQA/phiBelowQ3"), p3.phi());
      }

      ThreeBodyQARegistry.fill(HIST("TripletTaskQA/particle_pT_in_Triplet_SE"), p1.pt(), p2.pt(), p3.pt(), Q3);
      sameEventCont.setTriplet<isMC>(p1, p2, p3, multCol, Q3);
      ThreeBodyQARegistry.fill(HIST("TripletTaskQA/hCentrality"), centCol, Q3);
      if (Q3<ConfMaxQ3ForkStarPlots){
        float k12 = FemtoDreamMath::getkstar(p1, mMassOne, p2, mMassTwo);
        float k13 = FemtoDreamMath::getkstar(p1, mMassOne, p3, mMassThree);
        float k23 = FemtoDreamMath::getkstar(p2, mMassTwo, p3, mMassThree);
        double tempkij[3] = {k12, k13, k23};
        std::sort(tempkij, tempkij + 3);
        ThreeBodyQARegistry.fill(HIST("TripletTaskQA/kstartkstarLargestLargest"),tempkij[1], tempkij[2]);
        ThreeBodyQARegistry.fill(HIST("TripletTaskQA/kstartkstarLargestSmallest"), tempkij[0], tempkij[2]);
      }
      //std::cout<<tempkij[0]<<" "<<tempkij[1]<<" "<<tempkij[2]<<std::endl;
      /*if(tempkij[1]>0.6 && tempkij[2]>0.6){
        trackHistoSelectedParts.fillQA<isMC, false>(p1, aod::femtodreamparticle::kPt, multCol, centCol);
        trackHistoSelectedParts.fillQA<isMC, false>(p2, aod::femtodreamparticle::kPt, multCol, centCol);
        trackHistoSelectedParts.fillQA<isMC, false>(p3, aod::femtodreamparticle::kPt, multCol, centCol);
        ThreeBodyQARegistry.fill(HIST("TripletTaskQA/kstartkstarLargestLargest"),tempkij[1], tempkij[2]);
        ThreeBodyQARegistry.fill(HIST("TripletTaskQA/kstartkstarLargestSmallest"), tempkij[0], tempkij[2]);
      }*/

    }
    ThreeBodyQARegistry.fill(HIST("TripletTaskQA/hTripletsPerEventBelow14"), numberOfTriplets);
  }

  /// process function to call doSameEvent with Data
  /// \param col subscribe to the collision table (Data)
  /// \param parts subscribe to the femtoDreamParticleTable
  void processSameEvent(FilteredFDCollision& col,
                        o2::aod::FDParticles& parts)
  {
    fillCollision<false>(col);
    auto thegroupSelectedParts = SelectedParts->sliceByCached(aod::femtodreamparticle::fdCollisionId, col.globalIndex(), cache);
    for (auto& part : thegroupSelectedParts) {
      trackHistoALLSelectedParts.fillQA<false, false>(part, aod::femtodreamparticle::kPt, col.multNtr(), col.multV0M());
    }

    if (thegroupSelectedParts.size() < 3) {
      return;
    }
    doSameEvent<false>(thegroupSelectedParts, parts, col.magField(), col.multNtr(), col.multV0M());
  }
  PROCESS_SWITCH(femtoDreamTripletTaskTrackTrackTrack, processSameEvent, "Enable processing same event", true);

  /// process function to call doSameEvent with Data which has a mask for containing particles or not
  /// \param col subscribe to the collision table (Data)
  /// \param parts subscribe to the femtoDreamParticleTable
  void processSameEventMasked(MaskedCollision& col, o2::aod::FDParticles& parts)
  {
    fillCollision<false>(col);
    auto thegroupSelectedParts = SelectedParts->sliceByCached(aod::femtodreamparticle::fdCollisionId, col.globalIndex(), cache);
    for (auto& part : thegroupSelectedParts) {
      trackHistoALLSelectedParts.fillQA<false, false>(part, aod::femtodreamparticle::kPt, col.multNtr(), col.multV0M());
    }
    if (thegroupSelectedParts.size() < 3) {
      return;
    }
    doSameEvent<false>(thegroupSelectedParts, parts, col.magField(), col.multNtr(), col.multV0M());
  }
  PROCESS_SWITCH(femtoDreamTripletTaskTrackTrackTrack, processSameEventMasked, "Enable processing same event with masks", false);

  /// process function for to call doSameEvent with Monte Carlo
  /// \param col subscribe to the collision table (Monte Carlo Reconstructed reconstructed)
  /// \param parts subscribe to joined table FemtoDreamParticles and FemtoDreamMCLables to access Monte Carlo truth
  /// \param FemtoDreamMCParticles subscribe to the Monte Carlo truth table
  void processSameEventMC(MCCollision & col,
                          o2::aod::FDMCCollisions&,
                          soa::Join<o2::aod::FDParticles, o2::aod::FDMCLabels>& parts,
                          o2::aod::FDMCParticles&)
  {
    fillCollision<false>(col);
    auto thegroupSelectedParts = SelectedPartsMC->sliceByCached(aod::femtodreamparticle::fdCollisionId, col.globalIndex(), cache);
    for (auto& part : thegroupSelectedParts) {
      trackHistoALLSelectedParts.fillQA<true, false>(part, aod::femtodreamparticle::kPt, col.multNtr(), col.multV0M());
      ThreeBodyQARegistry.fill(HIST("TrackMC_QA/hMazzachi"), part.fdMCParticle().pt(), (part.pt() - part.fdMCParticle().pt()) / part.fdMCParticle().pt());
    }
    if (thegroupSelectedParts.size() < 3) {
      return;
    }
    doSameEvent<true>(thegroupSelectedParts, parts, col.magField(), col.multNtr(), col.multV0M());
  }
  PROCESS_SWITCH(femtoDreamTripletTaskTrackTrackTrack, processSameEventMC, "Enable processing same event for Monte Carlo", false);

  /// process function for to call doSameEvent with Monte Carlo which has a mask for containing particles or not
  /// \param col subscribe to the collision table (Monte Carlo Reconstructed reconstructed)
  /// \param parts subscribe to joined table FemtoDreamParticles and FemtoDreamMCLables to access Monte Carlo truth
  /// \param FemtoDreamMCParticles subscribe to the Monte Carlo truth table
  void processSameEventMCMasked(MaskedMCCollision& col,
                                o2::aod::FDMCCollisions&,
                                soa::Join<o2::aod::FDParticles, o2::aod::FDMCLabels>& parts,
                                o2::aod::FDMCParticles&)
  {
    fillCollision<false>(col);
    auto thegroupSelectedParts = SelectedPartsMC->sliceByCached(aod::femtodreamparticle::fdCollisionId, col.globalIndex(), cache);
    for (auto& part : thegroupSelectedParts) {
      trackHistoALLSelectedParts.fillQA<true, false>(part, aod::femtodreamparticle::kPt, col.multNtr(), col.multV0M());
      ThreeBodyQARegistry.fill(HIST("TrackMC_QA/hMazzachi"), part.fdMCParticle().pt(), (part.pt() - part.fdMCParticle().pt()) / part.fdMCParticle().pt());
    }
    if (thegroupSelectedParts.size() < 3) {
      return;
    }
    doSameEvent<true>(thegroupSelectedParts, parts, col.magField(), col.multNtr(), col.multV0M());
  }
  PROCESS_SWITCH(femtoDreamTripletTaskTrackTrackTrack, processSameEventMCMasked, "Enable processing same event for Monte Carlo", false);

  /// This function processes the mixed event
  /// \tparam PartitionType
  /// \tparam PartType
  /// \tparam isMC: enables Monte Carlo truth specific histograms
  /// \param groupPartsOne partition for the first particle passed by the process function
  /// \param groupPartsTwo partition for the second particle passed by the process function
  /// \param groupPartsThree partition for the third particle passed by the process function
  /// \param parts femtoDreamParticles table (in case of Monte Carlo joined with FemtoDreamMCLabels)
  /// \param magFieldTesla magnetic field of the collision
  /// \param multCol multiplicity of the collision
  template <bool isMC, typename PartitionType, typename PartType>
  void doMixedEvent(PartitionType groupPartsOne, PartitionType groupPartsTwo, PartitionType groupPartsThree, PartType parts, float magFieldTesla, int multCol)
  {
    for (auto& [p1, p2, p3] : combinations(CombinationsFullIndexPolicy(groupPartsOne, groupPartsTwo, groupPartsThree))) {

      auto Q3 = FemtoDreamMath::getQ3(p1, mMassOne, p2, mMassTwo, p3, mMassThree);
      if (ConfIsCPR.value) {
        if (pairCloseRejectionME.isClosePair(p1, p2, parts, magFieldTesla, Q3)) {
          continue;
        }
        if (pairCloseRejectionME.isClosePair(p2, p3, parts, magFieldTesla, Q3)) {
          continue;
        }

        if (pairCloseRejectionME.isClosePair(p1, p3, parts, magFieldTesla, Q3)) {
          continue;
        }
      }
      // fill pT of all three particles as a function of Q3 for lambda calculations
      ThreeBodyQARegistry.fill(HIST("TripletTaskQA/particle_pT_in_Triplet_ME"), p1.pt(), p2.pt(), p3.pt(), Q3);
      ThreeBodyQARegistry.fill(HIST("TripletTaskQA/phiVSdPhiME"), p1.phi(), p1.phi()-p2.phi()); 
      ThreeBodyQARegistry.fill(HIST("TripletTaskQA/phiVSdPhiME"), p2.phi(), p2.phi()-p3.phi()); 
      ThreeBodyQARegistry.fill(HIST("TripletTaskQA/phiVSdPhiME"), p3.phi(), p3.phi()-p1.phi()); 
      
      if (Q3<ConfMaxQ3ForkStarPlots){
        float k12 = FemtoDreamMath::getkstar(p1, mMassOne, p2, mMassTwo);
        float k13 = FemtoDreamMath::getkstar(p1, mMassOne, p3, mMassThree);
        float k23 = FemtoDreamMath::getkstar(p2, mMassTwo, p3, mMassThree);
        double tempkij[3] = {k12, k13, k23};
        std::sort(tempkij, tempkij + 3);
        //std::cout<<tempkij[0]<<" "<<tempkij[1]<<" "<<tempkij[2]<<std::endl;
        ThreeBodyQARegistry.fill(HIST("TripletTaskQA/kstartkstarLargestLargestME"),tempkij[1], tempkij[2]);
        ThreeBodyQARegistry.fill(HIST("TripletTaskQA/kstartkstarLargestSmallestME"), tempkij[0], tempkij[2]);
      }
      mixedEventCont.setTriplet<isMC>(p1, p2, p3, multCol, Q3);
    }
  }

  /// process function for to call doMixedEvent with Data
  /// @param cols subscribe to the collisions table (Data)
  /// @param parts subscribe to the femtoDreamParticleTable
  void processMixedEvent(FilteredFDCollisions& cols,
                         o2::aod::FDParticles& parts)
  {
    for (auto& [collision1, collision2, collision3] : soa::selfCombinations(colBinning, ConfNEventsMix, -1, cols, cols, cols)) {
      const int multiplicityCol = collision1.multNtr();
      ThreeBodyQARegistry.fill(HIST("TripletTaskQA/hMECollisionBins"), colBinning.getBin({collision1.posZ(), multiplicityCol}));

      auto groupPartsOne = SelectedParts->sliceByCached(aod::femtodreamparticle::fdCollisionId, collision1.globalIndex(), cache);
      auto groupPartsTwo = SelectedParts->sliceByCached(aod::femtodreamparticle::fdCollisionId, collision2.globalIndex(), cache);
      auto groupPartsThree = SelectedParts->sliceByCached(aod::femtodreamparticle::fdCollisionId, collision3.globalIndex(), cache);

      const auto& magFieldTesla1 = collision1.magField();
      const auto& magFieldTesla2 = collision2.magField();
      const auto& magFieldTesla3 = collision3.magField();

      if ((magFieldTesla1 != magFieldTesla2) || (magFieldTesla2 != magFieldTesla3) || (magFieldTesla1 != magFieldTesla3)) {
        continue;
      }

      doMixedEvent<false>(groupPartsOne, groupPartsTwo, groupPartsThree, parts, magFieldTesla1, multiplicityCol);
    }
  }
  PROCESS_SWITCH(femtoDreamTripletTaskTrackTrackTrack, processMixedEvent, "Enable processing mixed events", true);

  /// process function for to call doMixedEvent with Data which has a mask for containing particles or not
  /// @param cols subscribe to the collisions table (Data)
  /// @param parts subscribe to the femtoDreamParticleTable
  void processMixedEventMasked(MaskedCollisions& cols, o2::aod::FDParticles& parts)
  {
    Partition<MaskedCollisions> PartitionMaskedCol1 = (ConfTracksInMixedEvent == 1 && (aod::femtodreamcollision::bitmaskTrackOne & MaskBit) == MaskBit) ||
                                                      (ConfTracksInMixedEvent == 2 && (aod::femtodreamcollision::bitmaskTrackTwo & MaskBit) == MaskBit) ||
                                                      (ConfTracksInMixedEvent == 3 && (aod::femtodreamcollision::bitmaskTrackThree & MaskBit) == MaskBit);

    PartitionMaskedCol1.bindTable(cols);

    for (auto& [collision1, collision2, collision3] : soa::selfCombinations(colBinning, ConfNEventsMix, -1, *PartitionMaskedCol1.mFiltered, *PartitionMaskedCol1.mFiltered, *PartitionMaskedCol1.mFiltered)) {
      const int multiplicityCol = collision1.multNtr();
      ThreeBodyQARegistry.fill(HIST("TripletTaskQA/hMECollisionBins"), colBinning.getBin({collision1.posZ(), multiplicityCol}));

      auto groupPartsOne = SelectedParts->sliceByCached(aod::femtodreamparticle::fdCollisionId, collision1.globalIndex(), cache);
      auto groupPartsTwo = SelectedParts->sliceByCached(aod::femtodreamparticle::fdCollisionId, collision2.globalIndex(), cache);
      auto groupPartsThree = SelectedParts->sliceByCached(aod::femtodreamparticle::fdCollisionId, collision3.globalIndex(), cache);

      const auto& magFieldTesla1 = collision1.magField();
      const auto& magFieldTesla2 = collision2.magField();
      const auto& magFieldTesla3 = collision3.magField();

      if ((magFieldTesla1 != magFieldTesla2) || (magFieldTesla2 != magFieldTesla3) || (magFieldTesla1 != magFieldTesla3)) {
        continue;
      }

      doMixedEvent<false>(groupPartsOne, groupPartsTwo, groupPartsThree, parts, magFieldTesla1, multiplicityCol);
    }
  }
  PROCESS_SWITCH(femtoDreamTripletTaskTrackTrackTrack, processMixedEventMasked, "Enable processing mixed events", false);

  /// brief process function for to call doMixedEvent with Monte Carlo
  /// @param cols subscribe to the collisions table (Monte Carlo Reconstructed reconstructed)
  /// @param parts subscribe to joined table FemtoDreamParticles and FemtoDreamMCLables to access Monte Carlo truth
  /// @param FemtoDreamMCParticles subscribe to the Monte Carlo truth table
  void processMixedEventMC(MCCollisions& cols,
                           o2::aod::FDMCCollisions&,
                           soa::Join<o2::aod::FDParticles, o2::aod::FDMCLabels>& parts,
                           o2::aod::FDMCParticles&)
  {
    for (auto& [collision1, collision2, collision3] : soa::selfCombinations(colBinning, ConfNEventsMix, -1, cols, cols, cols)) {

      const int multiplicityCol = collision1.multNtr();
      ThreeBodyQARegistry.fill(HIST("TripletTaskQA/hMECollisionBins"), colBinning.getBin({collision1.posZ(), multiplicityCol}));

      auto groupPartsOne = SelectedPartsMC->sliceByCached(aod::femtodreamparticle::fdCollisionId, collision1.globalIndex(), cache);
      auto groupPartsTwo = SelectedPartsMC->sliceByCached(aod::femtodreamparticle::fdCollisionId, collision2.globalIndex(), cache);
      auto groupPartsThree = SelectedPartsMC->sliceByCached(aod::femtodreamparticle::fdCollisionId, collision3.globalIndex(), cache);

      const auto& magFieldTesla1 = collision1.magField();
      const auto& magFieldTesla2 = collision2.magField();
      const auto& magFieldTesla3 = collision3.magField();

      if ((magFieldTesla1 != magFieldTesla2) || (magFieldTesla2 != magFieldTesla3) || (magFieldTesla1 != magFieldTesla3)) {
        continue;
      }
      // CONSIDER testing different strategies to which events to use

      doMixedEvent<true>(groupPartsOne, groupPartsTwo, groupPartsThree, parts, magFieldTesla1, multiplicityCol);
    }
  }
  PROCESS_SWITCH(femtoDreamTripletTaskTrackTrackTrack, processMixedEventMC, "Enable processing mixed events MC", false);

  /// brief process function for to call doMixedEvent with Monte Carlo which has a mask for containing particles or not
  /// @param cols subscribe to the collisions table (Monte Carlo Reconstructed reconstructed)
  /// @param parts subscribe to joined table FemtoDreamParticles and FemtoDreamMCLables to access Monte Carlo truth
  /// @param FemtoDreamMCParticles subscribe to the Monte Carlo truth table
  void processMixedEventMCMasked(MaskedMCCollisions& cols,
                                 o2::aod::FDMCCollisions&,
                                 soa::Join<o2::aod::FDParticles, o2::aod::FDMCLabels>& parts,
                                 o2::aod::FDMCParticles&)
  {
    Partition<MaskedMCCollisions> PartitionMaskedCol1 = (ConfTracksInMixedEvent == 1 && (aod::femtodreamcollision::bitmaskTrackOne & MaskBit) == MaskBit) ||
                                                      (ConfTracksInMixedEvent == 2 && (aod::femtodreamcollision::bitmaskTrackTwo & MaskBit) == MaskBit) ||
                                                      (ConfTracksInMixedEvent == 3 && (aod::femtodreamcollision::bitmaskTrackThree & MaskBit) == MaskBit);
    PartitionMaskedCol1.bindTable(cols);

    for (auto& [collision1, collision2, collision3] : soa::selfCombinations(colBinning, ConfNEventsMix, -1, *PartitionMaskedCol1.mFiltered, *PartitionMaskedCol1.mFiltered, *PartitionMaskedCol1.mFiltered)) {

      const int multiplicityCol = collision1.multNtr();
      ThreeBodyQARegistry.fill(HIST("TripletTaskQA/hMECollisionBins"), colBinning.getBin({collision1.posZ(), multiplicityCol}));

      auto groupPartsOne = SelectedPartsMC->sliceByCached(aod::femtodreamparticle::fdCollisionId, collision1.globalIndex(), cache);
      auto groupPartsTwo = SelectedPartsMC->sliceByCached(aod::femtodreamparticle::fdCollisionId, collision2.globalIndex(), cache);
      auto groupPartsThree = SelectedPartsMC->sliceByCached(aod::femtodreamparticle::fdCollisionId, collision3.globalIndex(), cache);

      const auto& magFieldTesla1 = collision1.magField();
      const auto& magFieldTesla2 = collision2.magField();
      const auto& magFieldTesla3 = collision3.magField();

      if ((magFieldTesla1 != magFieldTesla2) || (magFieldTesla2 != magFieldTesla3) || (magFieldTesla1 != magFieldTesla3)) {
        continue;
      }
      // CONSIDER testing different strategies to which events to use

      doMixedEvent<true>(groupPartsOne, groupPartsTwo, groupPartsThree, parts, magFieldTesla1, multiplicityCol);
    }
  }
  PROCESS_SWITCH(femtoDreamTripletTaskTrackTrackTrack, processMixedEventMCMasked, "Enable processing mixed events MC", false);
};

WorkflowSpec defineDataProcessing(ConfigContext const& cfgc)
{
  WorkflowSpec workflow{
    adaptAnalysisTask<femtoDreamTripletTaskTrackTrackTrack>(cfgc),
  };
  return workflow;
}
