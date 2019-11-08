// Copyright (c) 2017, Lawrence Livermore National Security, LLC.
// Produced at the Lawrence Livermore National Laboratory (LLNL).
// Written by Cosmin G. Petra, petra1@llnl.gov.
// LLNL-CODE-742473. All rights reserved.
//
// This file is part of HiOp. For details, see https://github.com/LLNL/hiop. HiOp 
// is released under the BSD 3-clause license (https://opensource.org/licenses/BSD-3-Clause). 
// Please also read “Additional BSD Notice” below.
//
// Redistribution and use in source and binary forms, with or without modification, 
// are permitted provided that the following conditions are met:
// i. Redistributions of source code must retain the above copyright notice, this list 
// of conditions and the disclaimer below.
// ii. Redistributions in binary form must reproduce the above copyright notice, 
// this list of conditions and the disclaimer (as noted below) in the documentation and/or 
// other materials provided with the distribution.
// iii. Neither the name of the LLNS/LLNL nor the names of its contributors may be used to 
// endorse or promote products derived from this software without specific prior written 
// permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY 
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
// OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
// SHALL LAWRENCE LIVERMORE NATIONAL SECURITY, LLC, THE U.S. DEPARTMENT OF ENERGY OR 
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
// OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED 
// AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, 
// EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Additional BSD Notice
// 1. This notice is required to be provided under our contract with the U.S. Department 
// of Energy (DOE). This work was produced at Lawrence Livermore National Laboratory under 
// Contract No. DE-AC52-07NA27344 with the DOE.
// 2. Neither the United States Government nor Lawrence Livermore National Security, LLC 
// nor any of their employees, makes any warranty, express or implied, or assumes any 
// liability or responsibility for the accuracy, completeness, or usefulness of any 
// information, apparatus, product, or process disclosed, or represents that its use would
// not infringe privately-owned rights.
// 3. Also, reference herein to any specific commercial products, process, or services by 
// trade name, trademark, manufacturer or otherwise does not necessarily constitute or 
// imply its endorsement, recommendation, or favoring by the United States Government or 
// Lawrence Livermore National Security, LLC. The views and opinions of authors expressed 
// herein do not necessarily state or reflect those of the United States Government or 
// Lawrence Livermore National Security, LLC, and shall not be used for advertising or 
// product endorsement purposes.

#ifndef HIOP_NLP_FORMULATION
#define HIOP_NLP_FORMULATION

#include "hiopInterface.hpp"
#include "hiopVector.hpp"
#include "hiopMatrix.hpp"
#include "hiopMatrixMDS.hpp"

#ifdef HIOP_USE_MPI
#include "mpi.h"  
#endif

#include "hiopNlpTransforms.hpp"

#include "hiopRunStats.hpp"
#include "hiopLogger.hpp"
#include "hiopOptions.hpp"

#include <cstring>

namespace hiop
{

/** Class for a general NlpFormulation with general constraints and bounds on the variables. 
 * This class also  acts as a factory for linear algebra objects (derivative 
 * matrices, KKT system) whose types are decided based on the hiopInterfaceXXX object passed in the
 * constructor.
 * 
 * This formulation assumes that optimiz variables, rhs, and gradient are VECTORS: contiguous 
 * double arrays for which only local part is accessed (no inter-process comm).
 * Derivatives are generic MATRICES, whose type depend on 
 *    i.  the NLP formulation (sparse general or NLP with few dense constraints) 
 *   ii. the interface provided (general sparse (not yet supported), mixed sparse-dense, or dense
 * constraints).
 * Exact matching of MATRICES and hiopInterface is to be done by specializations of this class.
 */
class hiopNlpFormulation
{
public:
  hiopNlpFormulation(hiopInterfaceBase& interface);
  virtual ~hiopNlpFormulation();

  virtual bool finalizeInitialization();

  /* wrappers for the interface calls. Can be overridden for specialized formulations required by the algorithm */
  virtual bool eval_f(double* x, bool new_x, double& f);
  virtual bool eval_grad_f(double* x, bool new_x, double* gradf);
  virtual bool eval_c(double* x, bool new_x, double* c);
  virtual bool eval_d(double* x, bool new_x, double* d);
  /* the implementation of the next two methods depends both on the interface and on the formulation */
  virtual bool eval_Jac_c(double* x, bool new_x, hiopMatrix& Jac_c)=0;
  virtual bool eval_Jac_d(double* x, bool new_x, hiopMatrix& Jac_d)=0;
  virtual bool eval_Hess_Lagr(double* x, bool new_x, const double& obj_factor, 
			      double* lambda, bool new_lambda, hiopMatrix& Hess_L)=0;
  /* starting point */
  virtual bool get_starting_point(hiopVector& x0);

  /** linear algebra factory */
  virtual hiopVector* alloc_primal_vec() const;
  virtual hiopVector* alloc_dual_eq_vec() const;
  virtual hiopVector* alloc_dual_ineq_vec() const;
  virtual hiopVector* alloc_dual_vec() const;
  /* the implementation of the next two methods depends both on the interface and on the formulation */
  virtual hiopMatrix* alloc_Jac_c() const=0;
  virtual hiopMatrix* alloc_Jac_d() const=0;

  virtual inline 
  void user_callback_solution(hiopSolveStatus status,
			      const hiopVector& x,
			      const hiopVector& z_L,
			      const hiopVector& z_U,
			      const hiopVector& c, const hiopVector& d,
			      const hiopVector& yc, const hiopVector& yd,
			      double obj_value) 
  {
    const hiopVectorPar& xp = dynamic_cast<const hiopVectorPar&>(x);
    const hiopVectorPar& zl = dynamic_cast<const hiopVectorPar&>(z_L);
    const hiopVectorPar& zu = dynamic_cast<const hiopVectorPar&>(z_U);
    assert(xp.get_size()==n_vars);
    assert(c.get_size()+d.get_size()==n_cons);
    //!petra: to do: assemble (c,d) into cons and (yc,yd) into lambda based on cons_eq_mapping and cons_ineq_mapping
    interface_base.solution_callback(status, 
				     (int)n_vars, xp.local_data_const(), zl.local_data_const(), zu.local_data_const(),
				     (int)n_cons, NULL, //cons, 
				     NULL, //lambda,
				     obj_value);
  }

  virtual inline 
  bool user_callback_iterate(int iter, double obj_value,
			     const hiopVector& x, const hiopVector& z_L, const hiopVector& z_U,
			     const hiopVector& c, const hiopVector& d, const hiopVector& yc, const hiopVector& yd,
			     double inf_pr, double inf_du, double mu, double alpha_du, double alpha_pr, int ls_trials)
  {
    const hiopVectorPar& xp = dynamic_cast<const hiopVectorPar&>(x);
    const hiopVectorPar& zl = dynamic_cast<const hiopVectorPar&>(z_L);
    const hiopVectorPar& zu = dynamic_cast<const hiopVectorPar&>(z_U);
    assert(xp.get_size()==n_vars);
    assert(c.get_size()+d.get_size()==n_cons);
    //!petra: to do: assemble (c,d) into cons and (yc,yd) into lambda based on cons_eq_mapping and cons_ineq_mapping
    return interface_base.iterate_callback(iter, obj_value, 
					   (int)n_vars, xp.local_data_const(), zl.local_data_const(), zu.local_data_const(),
					   (int)n_cons, NULL, //cons, 
					   NULL, //lambda,
					   inf_pr, inf_du, mu, alpha_du, alpha_pr,  ls_trials);
  }
  
  /** const accessors */
  inline const hiopVectorPar& get_xl ()  const { return *xl;   }
  inline const hiopVectorPar& get_xu ()  const { return *xu;   }
  inline const hiopVectorPar& get_ixl()  const { return *ixl;  }
  inline const hiopVectorPar& get_ixu()  const { return *ixu;  }
  inline const hiopVectorPar& get_dl ()  const { return *dl;   }
  inline const hiopVectorPar& get_du ()  const { return *du;   }
  inline const hiopVectorPar& get_idl()  const { return *idl;  }
  inline const hiopVectorPar& get_idu()  const { return *idu;  }
  inline const hiopVectorPar& get_crhs() const { return *c_rhs;}

  /** const accessors */
  inline long long n() const      {return n_vars;}
  inline long long m() const      {return n_cons;}
  inline long long m_eq() const   {return n_cons_eq;}
  inline long long m_ineq() const {return n_cons_ineq;}
  inline long long n_low() const  {return n_bnds_low;}
  inline long long n_upp() const  {return n_bnds_upp;}
  inline long long m_ineq_low() const {return n_ineq_low;}
  inline long long m_ineq_upp() const {return n_ineq_upp;}
  inline long long n_complem()  const {return m_ineq_low()+m_ineq_upp()+n_low()+n_upp();}

  inline long long n_local() const{return xl->get_local_size();}
  inline long long n_low_local() const {return n_bnds_low_local;}
  inline long long n_upp_local() const {return n_bnds_upp_local;}

  /* methods for transforming the internal objects to corresponding user objects */
  inline double user_obj(double hiop_f) { return nlp_transformations.applyToObj(hiop_f); }
  inline void   user_x(hiopVectorPar& hiop_x, double* user_x) 
  { 
    double *hiop_xa = hiop_x.local_data();
    double *user_xa = nlp_transformations.applyTox(hiop_xa,/*new_x=*/true); 
    //memcpy(user_x, user_xa, hiop_x.get_local_size()*sizeof(double));
    memcpy(user_x, user_xa, nlp_transformations.n_post_local()*sizeof(double));
  }

  /* outputing and debug-related functionality*/
  hiopLogger* log;
  hiopRunStats runStats;
  hiopOptions* options;
  //prints a summary of the problem
  virtual void print(FILE* f=NULL, const char* msg=NULL, int rank=-1) const;
#ifdef HIOP_USE_MPI
  inline MPI_Comm get_comm() const { return comm; }
  inline int      get_rank() const { return rank; }
  inline int      get_num_ranks() const { return num_ranks; }
#endif
protected:
#ifdef HIOP_USE_MPI
  MPI_Comm comm;
  int rank, num_ranks;
  bool mpi_init_called;
#endif

  /* problem data */
  //various dimensions
  long long n_vars, n_cons, n_cons_eq, n_cons_ineq;
  long long n_bnds_low, n_bnds_low_local, n_bnds_upp, n_bnds_upp_local, n_ineq_low, n_ineq_upp;
  long long n_bnds_lu, n_ineq_lu;
  hiopVectorPar *xl, *xu, *ixu, *ixl; //these will/can be global, memory distributed
  hiopInterfaceBase::NonlinearityType* vars_type; //C array containing the types for local vars

  hiopVectorPar *c_rhs; //local
  hiopInterfaceBase::NonlinearityType* cons_eq_type;

  hiopVectorPar *dl, *du,  *idl, *idu; //these will be local
  hiopInterfaceBase::NonlinearityType* cons_ineq_type;
  // keep track of the constraints indexes in the original, user's formulation
  long long *cons_eq_mapping, *cons_ineq_mapping; 

  //options for which this class was setup
  std::string strFixedVars; //"none", "fixed", "relax"
  double dFixedVarsTol;

  //internal NLP transformations (currently fixing/relaxing variables implemented)
  hiopNlpTransformations nlp_transformations;

#ifdef HIOP_USE_MPI
  //inter-process distribution of vectors
  long long* vec_distrib;
#endif

  hiopInterfaceBase& interface_base;

private:
  hiopNlpFormulation(const hiopNlpFormulation& s) : interface_base(s.interface_base) {};
};

/* *************************************************************************
 * Class is for NLPs that has a small number of general/dense constraints *
 * Splits the constraints in ineq and eq.
 * *************************************************************************
 */
class hiopNlpDenseConstraints : public hiopNlpFormulation
{
public:
  hiopNlpDenseConstraints(hiopInterfaceDenseConstraints& interface);
  virtual ~hiopNlpDenseConstraints();

  virtual bool finalizeInitialization();

  virtual bool eval_Jac_c(double* x, bool new_x, hiopMatrix& Jac_c)
  {
    hiopMatrixDense* Jac_cde = dynamic_cast<hiopMatrixDense*>(&Jac_c);
    if(Jac_cde==NULL) {
      log->printf(hovError, "[internal error] hiopNlpDenseConstraints NLP works only with dense matrices\n");
      return false;
    } else {
      return this->eval_Jac_c(x, new_x, Jac_cde->local_data());
    }
  }
  virtual bool eval_Jac_d(double* x, bool new_x, hiopMatrix& Jac_d)
  {
    hiopMatrixDense* Jac_dde = dynamic_cast<hiopMatrixDense*>(&Jac_d);
    if(Jac_dde==NULL) {
      log->printf(hovError, "[internal error] hiopNlpDenseConstraints NLP works only with dense matrices\n");
      return false;
    } else {
      return this->eval_Jac_d(x, new_x, Jac_dde->local_data());
    }
  }
  /* specialized evals to avoid overhead of dynamic cast. Generic variants available above. */
  virtual bool eval_Jac_c(double* x, bool new_x, double** Jac_c);
  virtual bool eval_Jac_d(double* x, bool new_x, double** Jac_d);
  virtual bool eval_Hess_Lagr(double* x, bool new_x, const double& obj_factor, 
			      double* lambda, bool new_lambda, hiopMatrix& Hess_L)
  {
    assert(false && "this NLP formulation is only for Quasi-Newton");
    return true;
  }


  virtual hiopMatrixDense* alloc_Jac_c() const;
  virtual hiopMatrixDense* alloc_Jac_d() const;

  /* this is in general for a dense matrix witn n_vars cols and a small number of 
   * 'nrows' rows. The second argument indicates how much total memory should the
   * matrix (pre)allocate.
   */
  virtual hiopMatrixDense* alloc_multivector_primal(int nrows, int max_rows=-1) const;

private:
  /* interface implemented and provided by the user */
  hiopInterfaceDenseConstraints& interface;
};



/* *************************************************************************
 * Class is for general NLPs that have mixed sparse-dense (MDS) derivatives
 * blocks. 
 * *************************************************************************
 */
class hiopNlpMDS : public hiopNlpFormulation
{
public:
  hiopNlpMDS(hiopInterfaceMDS& interface_);
  virtual ~hiopNlpMDS() {};
  virtual bool eval_Jac_c(double* x, bool new_x, hiopMatrix& Jac_c)
  {
    hiopMatrixMDS* pJac_c = dynamic_cast<hiopMatrixMDS*>(&Jac_c);
    assert(pJac_c);
    if(pJac_c) {
      int nnz = pJac_c->sp_nnz();
      return interface.eval_Jac_cons(n_vars, n_cons, 
				     n_cons_eq, cons_eq_mapping, 
				     x, new_x, pJac_c->n_sp(), pJac_c->n_de(), 
				     nnz, pJac_c->sp_irow(), pJac_c->sp_jcol(), pJac_c->sp_M(),
				     pJac_c->de_local_data());
    } else {
      return false;
    }
  }
  virtual bool eval_Jac_d(double* x, bool new_x, hiopMatrix& Jac_d)
  {
    hiopMatrixMDS* pJac_d = dynamic_cast<hiopMatrixMDS*>(&Jac_d);
    assert(pJac_d);
    if(pJac_d) {
      int nnz = pJac_d->sp_nnz();
      return interface.eval_Jac_cons(n_vars, n_cons, 
				     n_cons_ineq, cons_ineq_mapping, 
				     x, new_x, pJac_d->n_sp(), pJac_d->n_de(), 
				     nnz, pJac_d->sp_irow(), pJac_d->sp_jcol(), pJac_d->sp_M(),
				     pJac_d->de_local_data());
    } else {
      return false;
    }
  }
  virtual bool eval_Hess_Lagr(double* x, bool new_x, const double& obj_factor, 
			      double* lambda, bool new_lambda, hiopMatrix& Hess_L)
  {
    hiopMatrixSymBlockDiagMDS* pHessL = dynamic_cast<hiopMatrixSymBlockDiagMDS*>(&Hess_L);
    assert(pHessL);
    if(pHessL) {
      int nnzHSS = pHessL->sp_nnz(), nnzHSD = 0;
      bool bret = interface.eval_Hess_Lagr(n_vars, n_cons, x, new_x, obj_factor, lambda, new_lambda, 
				      pHessL->n_sp(), pHessL->n_de(),
				      nnzHSS, pHessL->sp_irow(), pHessL->sp_jcol(), pHessL->sp_M(),
				      pHessL->de_local_data(),
				      nnzHSD, NULL, NULL, NULL);
      assert(nnzHSD==0);
      assert(nnzHSS==pHessL->sp_nnz());
      return bret;
    } else {
      return false;
    }
  }
  virtual hiopMatrix* alloc_Jac_c() const;
  virtual hiopMatrix* alloc_Jac_d() const;
private:
  hiopInterfaceMDS& interface;
};

}
#endif
