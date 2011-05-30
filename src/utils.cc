#include "../interface/utils.h"

#include <cstdio>
#include <iostream>
#include <cmath>
#include <vector>
#include <string>
#include <memory>
#include <typeinfo>
#include <stdexcept>

#include <TIterator.h>
#include <TString.h>

#include <RooAbsData.h>
#include <RooAbsPdf.h>
#include <RooArgSet.h>
#include <RooDataHist.h>
#include <RooDataSet.h>
#include <RooRealVar.h>
#include <RooProdPdf.h>
#include <RooSimultaneous.h>
#include <RooWorkspace.h>
#include <RooStats/ModelConfig.h>

void utils::printRDH(RooAbsData *data) {
  std::vector<std::string> varnames, catnames;
  const RooArgSet *b0 = data->get(0);
  TIterator *iter = b0->createIterator();
  for (RooAbsArg *a = 0; (a = (RooAbsArg *)iter->Next()) != 0; ) {
    if (a->InheritsFrom("RooRealVar")) {
      varnames.push_back(a->GetName());
    } else if (a->InheritsFrom("RooCategory")) {
      catnames.push_back(a->GetName());
    }
  }
  delete iter;
  size_t nv = varnames.size(), nc = catnames.size();
  printf(" bin  ");
  for (size_t j = 0; j < nv; ++j) { printf("%10.10s  ", varnames[j].c_str()); }
  for (size_t j = 0; j < nc; ++j) { printf("%10.10s  ", catnames[j].c_str()); }
  printf("  weight\n");
  for (int i = 0, nb = data->numEntries(); i < nb; ++i) {
    const RooArgSet *bin = data->get(i);
    printf("%4d  ",i);
    for (size_t j = 0; j < nv; ++j) { printf("%10g  ",    bin->getRealValue(varnames[j].c_str())); }
    for (size_t j = 0; j < nc; ++j) { printf("%10.10s  ", bin->getCatLabel(catnames[j].c_str())); }
    printf("%8.3f\n", data->weight());
  }
}

void utils::printRAD(const RooAbsData *d) {
  if (d->InheritsFrom("RooDataHist") || d->numEntries() != 1) printRDH(const_cast<RooAbsData*>(d));
  else d->get(0)->Print("V");
}

void utils::printPdf(RooAbsPdf *pdf) {
  std::cout << "Pdf " << pdf->GetName() << " parameters." << std::endl;
  std::auto_ptr<RooArgSet> params(pdf->getVariables());
  params->Print("V");
}


void utils::printPdf(RooStats::ModelConfig &mc) {
  std::cout << "ModelConfig " << mc.GetName() << " (" << mc.GetTitle() << "): pdf parameters." << std::endl;
  std::auto_ptr<RooArgSet> params(mc.GetPdf()->getVariables());
  params->Print("V");
}

void utils::printPdf(RooWorkspace *w, const char *pdfName) {
  std::cout << "PDF " << pdfName << " parameters." << std::endl;
  std::auto_ptr<RooArgSet> params(w->pdf(pdfName)->getVariables());
  params->Print("V");
}

RooAbsPdf *utils::factorizePdf(const RooArgSet &observables, RooAbsPdf &pdf, RooArgList &constraints) {
    const std::type_info & id = typeid(pdf);
    if (id == typeid(RooProdPdf)) {
        RooProdPdf *prod = dynamic_cast<RooProdPdf *>(&pdf);
        RooArgList newFactors; RooArgSet newOwned;
        RooArgList list(prod->pdfList());
        bool needNew = false;
        for (int i = 0, n = list.getSize(); i < n; ++i) {
            RooAbsPdf *pdfi = (RooAbsPdf *) list.at(i);
            RooAbsPdf *newpdf = factorizePdf(observables, *pdfi, constraints);
            if (newpdf == 0) { needNew = true; continue; }
            if (newpdf != pdfi) { needNew = true; newOwned.add(*newpdf); }
            newFactors.add(*newpdf);
        }
        if (!needNew) return prod;
        else if (newFactors.getSize() == 0) return 0;
        else if (newFactors.getSize() == 1) return (RooAbsPdf *) newFactors.first()->Clone(TString::Format("%s_obsOnly", pdf.GetName()));
        RooProdPdf *ret = new RooProdPdf(TString::Format("%s_obsOnly", pdf.GetName()), "", newFactors);
        ret->addOwnedComponents(newOwned);
        return ret;
    } else if (id == typeid(RooSimultaneous)) {
        RooSimultaneous *sim  = dynamic_cast<RooSimultaneous *>(&pdf);
        RooAbsCategoryLValue *cat = (RooAbsCategoryLValue *) sim->indexCat().Clone();
        int nbins = cat->numBins((const char *)0);
        TObjArray factorizedPdfs(nbins); RooArgSet newOwned;
        bool needNew = false;
        for (int ic = 0, nc = nbins; ic < nc; ++ic) {
            cat->setBin(ic);
            RooAbsPdf *pdfi = sim->getPdf(cat->getLabel());
            RooAbsPdf *newpdf = factorizePdf(observables, *pdfi, constraints);
            factorizedPdfs[ic] = newpdf;
            if (newpdf == 0) { needNew = true; continue; }
            if (newpdf != pdfi) { needNew = true; newOwned.add(*newpdf); }
        }
        RooSimultaneous *ret = sim;
        if (needNew) {
            ret = new RooSimultaneous(TString::Format("%s_obsOnly", pdf.GetName()), "", (RooAbsCategoryLValue&) sim->indexCat());
            for (int ic = 0, nc = nbins; ic < nc; ++ic) {
                cat->setBin(ic);
                RooAbsPdf *newpdf = (RooAbsPdf *) factorizedPdfs[ic];
                if (newpdf) ret->addPdf(*newpdf, cat->getLabel());
            }
            ret->addOwnedComponents(newOwned);
        }
        delete cat;
        return ret;
    } else if (pdf.dependsOn(observables)) {
        return &pdf;
    } else {
        if (!constraints.contains(pdf)) constraints.add(pdf);
        return 0;
    }

}

void utils::factorizePdf(RooStats::ModelConfig &model, RooAbsPdf &pdf, RooArgList &obsTerms, RooArgList &constraints, bool debug) {
    const std::type_info & id = typeid(pdf);
    if (id == typeid(RooProdPdf)) {
        RooProdPdf *prod = dynamic_cast<RooProdPdf *>(&pdf);
        RooArgList list(prod->pdfList());
        for (int i = 0, n = list.getSize(); i < n; ++i) {
            RooAbsPdf *pdfi = (RooAbsPdf *) list.at(i);
            factorizePdf(model, *pdfi, obsTerms, constraints);
        }
    } else if (id == typeid(RooSimultaneous)) {
        RooSimultaneous *sim  = dynamic_cast<RooSimultaneous *>(&pdf);
        RooAbsCategoryLValue *cat = (RooAbsCategoryLValue *) sim->indexCat().Clone();
        for (int ic = 0, nc = cat->numBins((const char *)0); ic < nc; ++ic) {
            cat->setBin(ic);
            factorizePdf(model, *sim->getPdf(cat->getLabel()), obsTerms, constraints);
        }
        delete cat;
    } else if (pdf.dependsOn(*model.GetObservables())) {
        if (!obsTerms.contains(pdf)) obsTerms.add(pdf);
    } else {
        if (!constraints.contains(pdf)) constraints.add(pdf);
    }
}

RooAbsPdf *utils::makeNuisancePdf(RooStats::ModelConfig &model, const char *name) { 
    RooArgList obsTerms, constraints;
    factorizePdf(model, *model.GetPdf(), obsTerms, constraints);
    return new RooProdPdf(name,"", constraints);
}

RooAbsPdf *utils::makeObsOnlyPdf(RooStats::ModelConfig &model, const char *name) { 
    RooArgList obsTerms, constraints;
    factorizePdf(model, *model.GetPdf(), obsTerms, constraints);
    return new RooProdPdf(name,"", obsTerms);
}

RooAbsPdf *utils::fullClonePdf(const RooAbsPdf *pdf, RooArgSet &holder, bool cloneLeafNodes) {
  // Clone all FUNC compents by copying all branch nodes
  RooArgSet tmp("RealBranchNodeList") ;
  pdf->branchNodeServerList(&tmp);
  tmp.snapshot(holder, cloneLeafNodes); 
  
  // Find the top level FUNC in the snapshot list
  return (RooAbsPdf*) holder.find(pdf->GetName());
}

void utils::getClients(const RooAbsCollection &values, const RooAbsCollection &allObjects, RooAbsCollection &clients) {
    std::auto_ptr<TIterator> iterAll(allObjects.createIterator());
    std::auto_ptr<TIterator> iterVal(values.createIterator());
    for (RooAbsArg *v = (RooAbsArg *) iterVal->Next(); v != 0; v = (RooAbsArg *) iterVal->Next()) {
        if (typeid(*v) != typeid(RooRealVar)) continue;
        std::auto_ptr<TIterator> clientIter(v->clientIterator());
        for (RooAbsArg *a = (RooAbsArg *) clientIter->Next(); a != 0; a = (RooAbsArg *) clientIter->Next()) {
            if (allObjects.containsInstance(*a) && !clients.containsInstance(*a)) clients.add(*a);
        }
    }
}

bool utils::setAllConstant(const RooAbsCollection &coll, bool constant) {
    bool changed = false;
    std::auto_ptr<TIterator> iter(coll.createIterator());
    for (RooAbsArg *a = (RooAbsArg *) iter->Next(); a != 0; a = (RooAbsArg *) iter->Next()) {
        RooRealVar *v = dynamic_cast<RooRealVar *>(a);
        if (v && (v->isConstant() != constant)) {
            changed = true;
            v->setConstant(constant);
        }
    }
    return changed;
}
