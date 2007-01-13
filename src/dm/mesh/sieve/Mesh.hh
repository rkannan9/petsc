#ifndef included_ALE_Mesh_hh
#define included_ALE_Mesh_hh

#ifndef  included_ALE_Numbering_hh
#include <Numbering.hh>
#endif

namespace ALE {
  template<typename Topology_>
  class Bundle : public ALE::ParallelObject {
  public:
    typedef Topology_                                      topology_type;
    typedef typename topology_type::point_type             point_type;
    typedef ALE::New::Section<topology_type, double>       real_section_type;
    typedef ALE::New::Section<topology_type, int>          int_section_type;
    typedef struct {double x, y, z;}                       split_value;
    typedef ALE::pair<point_type, split_value>             pair_type;
    typedef ALE::New::Section<topology_type, pair_type>    pair_section_type;
    typedef std::map<std::string, Obj<real_section_type> > real_sections_type;
    typedef std::map<std::string, Obj<int_section_type> >  int_sections_type;
    typedef std::map<std::string, Obj<pair_section_type> > pair_sections_type;
    typedef typename topology_type::send_overlap_type      send_overlap_type;
    typedef typename topology_type::recv_overlap_type      recv_overlap_type;
    typedef typename ALE::New::Completion<topology_type, point_type>::topology_type             comp_topology_type;
    typedef typename ALE::New::OverlapValues<send_overlap_type, comp_topology_type, point_type> send_section_type;
    typedef typename ALE::New::OverlapValues<recv_overlap_type, comp_topology_type, point_type> recv_section_type;
  protected:
    Obj<topology_type> _topology;
    bool               _distributed;
    real_sections_type _realSections;
    int_sections_type  _intSections;
    pair_sections_type _pairSections;
  public:
    Bundle(MPI_Comm comm, int debug = 0) : ALE::ParallelObject(comm, debug), _distributed(false) {
      this->_topology = new topology_type(comm, debug);
    };
    Bundle(const Obj<topology_type>& topology) : ALE::ParallelObject(topology->comm(), topology->debug()), _topology(topology), _distributed(false) {};
  public: // Accessors
    bool getDistributed() const {return this->_distributed;};
    void setDistributed(const bool distributed) {this->_distributed = distributed;};
    const Obj<topology_type>& getTopology() const {return this->_topology;};
    void setTopology(const Obj<topology_type>& topology) {this->_topology = topology;};
  public:
    bool hasRealSection(const std::string& name) {
      return this->_realSections.find(name) != this->_realSections.end();
    };
    const Obj<real_section_type>& getRealSection(const std::string& name) {
      if (this->_realSections.find(name) == this->_realSections.end()) {
        Obj<real_section_type> section = new real_section_type(this->_topology);

        if (this->_debug) {std::cout << "Creating new real section: " << name << std::endl;}
        this->_realSections[name] = section;
      }
      return this->_realSections[name];
    };
    void setRealSection(const std::string& name, const Obj<real_section_type>& section) {
      this->_realSections[name] = section;
    };
    Obj<std::set<std::string> > getRealSections() const {
      Obj<std::set<std::string> > names = std::set<std::string>();

      for(typename real_sections_type::const_iterator s_iter = this->_realSections.begin(); s_iter != this->_realSections.end(); ++s_iter) {
        names->insert(s_iter->first);
      }
      return names;
    };
    bool hasIntSection(const std::string& name) {
      return this->_intSections.find(name) != this->_intSections.end();
    };
    const Obj<int_section_type>& getIntSection(const std::string& name) {
      if (this->_intSections.find(name) == this->_intSections.end()) {
        Obj<int_section_type> section = new int_section_type(this->_topology);

        if (this->_debug) {std::cout << "Creating new int section: " << name << std::endl;}
        this->_intSections[name] = section;
      }
      return this->_intSections[name];
    };
    void setIntSection(const std::string& name, const Obj<int_section_type>& section) {
      this->_intSections[name] = section;
    };
    Obj<std::set<std::string> > getIntSections() const {
      Obj<std::set<std::string> > names = std::set<std::string>();

      for(typename int_sections_type::const_iterator s_iter = this->_intSections.begin(); s_iter != this->_intSections.end(); ++s_iter) {
        names->insert(s_iter->first);
      }
      return names;
    };
    bool hasPairSection(const std::string& name) {
      return this->_pairSections.find(name) != this->_pairSections.end();
    };
    const Obj<pair_section_type>& getPairSection(const std::string& name) {
      if (this->_pairSections.find(name) == this->_pairSections.end()) {
        Obj<pair_section_type> section = new pair_section_type(this->_topology);

        if (this->_debug) {std::cout << "Creating new pair section: " << name << std::endl;}
        this->_pairSections[name] = section;
      }
      return this->_pairSections[name];
    };
    void setPairSection(const std::string& name, const Obj<pair_section_type>& section) {
      this->_pairSections[name] = section;
    };
    Obj<std::set<std::string> > getPairSections() const {
      Obj<std::set<std::string> > names = std::set<std::string>();

      for(typename pair_sections_type::const_iterator s_iter = this->_pairSections.begin(); s_iter != this->_pairSections.end(); ++s_iter) {
        names->insert(s_iter->first);
      }
      return names;
    };
  public:
    // Printing
    friend std::ostream& operator<<(std::ostream& os, const split_value& s) {
      os << "(" << s.x << ", "<< s.y << ", "<< s.z << ")";
      return os;
    };
  };

  class Discretization : public ALE::ParallelObject {
  protected:
    std::map<int,int> _dim2dof;
    std::map<int,int> _dim2class;
  public:
    Discretization(MPI_Comm comm, const int debug = 0) : ParallelObject(comm, debug) {};
    ~Discretization() {};
  public:
    const double *getQuadraturePoints() {return NULL;};
    const double *getQuadratureWeights() {return NULL;};
    const double *getBasis() {return NULL;};
    const double *getBasisDerivatives() {return NULL;};
    int  getNumDof(const int dim) {return this->_dim2dof[dim];};
    void setNumDof(const int dim, const int numDof) {this->_dim2dof[dim] = numDof;};
    int  getDofClass(const int dim) {return this->_dim2class[dim];};
    void setDofClass(const int dim, const int dofClass) {this->_dim2class[dim] = dofClass;};
  };

  class BoundaryCondition : public ALE::ParallelObject {
  protected:
    std::string _labelName;
    double    (*_func)(const double []);
  public:
    BoundaryCondition(MPI_Comm comm, const int debug = 0) : ParallelObject(comm, debug) {};
    ~BoundaryCondition() {};
  public:
    const std::string& getLabelName() {return this->_labelName;};
    void setLabelName(const std::string& name) {this->_labelName = name;};
    void setFunction(double (*func)(const double [])) {this->_func = func;};
  public:
    double evaluate(const double coords[]) {return this->_func(coords);};
  };

  class Mesh : public Bundle<ALE::New::Topology<int, ALE::Sieve<int,int,int> > > {
  public:
    typedef int                                       point_type;
    typedef ALE::Sieve<point_type,int,int>            sieve_type;
    typedef ALE::New::Topology<int, sieve_type>       topology_type;
    typedef topology_type::patch_type                 patch_type;
    typedef Bundle<topology_type>                     base_type;
    typedef ALE::New::NumberingFactory<topology_type> NumberingFactory;
    typedef NumberingFactory::numbering_type          numbering_type;
    typedef NumberingFactory::order_type              order_type;
    typedef base_type::send_overlap_type              send_overlap_type;
    typedef base_type::recv_overlap_type              recv_overlap_type;
    typedef base_type::send_section_type              send_section_type;
    typedef base_type::recv_section_type              recv_section_type;
    typedef base_type::real_sections_type             real_sections_type;
    // PCICE BC
    typedef struct {double rho,u,v,p;}                bc_value_type;
    typedef std::map<int, bc_value_type>              bc_values_type;
    // PyLith BC
    typedef ALE::New::Section<topology_type, ALE::pair<int,double> > foliated_section_type;
  protected:
    int                   _dim;
    Obj<NumberingFactory> _factory;
    // PCICE BC
    bc_values_type        _bcValues;
    // PyLith BC
    Obj<foliated_section_type> _boundaries;
    // Discretization
    Obj<Discretization>    _discretization;
    Obj<BoundaryCondition> _boundaryCondition;
  public:
    Mesh(MPI_Comm comm, int dim, int debug = 0) : Bundle<ALE::New::Topology<int, ALE::Sieve<int,int,int> > >(comm, debug), _dim(dim) {
      this->_factory = NumberingFactory::singleton(debug);
      this->_boundaries = NULL;
      this->_discretization = new Discretization(comm, debug);
      this->_boundaryCondition = new BoundaryCondition(comm, debug);
    };
    Mesh(const Obj<topology_type>& topology, int dim) : Bundle<ALE::New::Topology<int, ALE::Sieve<int,int,int> > >(topology), _dim(dim) {
      this->_factory = NumberingFactory::singleton(topology->debug());
      this->_boundaries = NULL;
    };
  public: // Accessors
    int getDimension() const {return this->_dim;};
    void setDimension(const int dim) {this->_dim = dim;};
    const Obj<NumberingFactory>& getFactory() {return this->_factory;};
    const Obj<Discretization>&    getDiscretization() {return this->_discretization;};
    void setDiscretization(const Obj<Discretization>& discretization) {this->_discretization = discretization;};
    const Obj<BoundaryCondition>& getBoundaryCondition() {return this->_boundaryCondition;};
    void setBoundaryCondition(const Obj<BoundaryCondition>& boundaryCondition) {this->_boundaryCondition = boundaryCondition;};
  public: // Mesh geometry
    static void computeTriangleGeometry(const Obj<real_section_type>& coordinates, const point_type& e, double v0[], double J[], double invJ[], double& detJ) {
      const patch_type patch  = 0;
      const double    *coords = coordinates->restrict(patch, e);
      const int        dim    = 2;
      double           invDet;

      if (v0) {
        for(int d = 0; d < dim; d++) {
          v0[d] = coords[d];
        }
      }
      if (J) {
        for(int d = 0; d < dim; d++) {
          for(int f = 0; f < dim; f++) {
            J[d*dim+f] = 0.5*(coords[(f+1)*dim+d] - coords[0*dim+d]);
          }
        }
        detJ = J[0]*J[3] - J[1]*J[2];
      }
      if (invJ) {
        invDet  = 1.0/detJ;
        invJ[0] =  invDet*J[3];
        invJ[1] = -invDet*J[1];
        invJ[2] = -invDet*J[2];
        invJ[3] =  invDet*J[0];
      }
    };
    static void computeTetrahedronGeometry(const Obj<real_section_type>& coordinates, const point_type& e, double v0[], double J[], double invJ[], double& detJ) {
      const patch_type patch  = 0;
      const double    *coords = coordinates->restrict(patch, e);
      const int        dim    = 3;
      double           invDet;

      if (v0) {
        for(int d = 0; d < dim; d++) {
          v0[d] = coords[d];
        }
      }
      if (J) {
        for(int d = 0; d < dim; d++) {
          for(int f = 0; f < dim; f++) {
            J[d*dim+f] = 0.5*(coords[(f+1)*dim+d] - coords[0*dim+d]);
          }
        }
        detJ = J[0*3+0]*(J[1*3+1]*J[2*3+2] - J[1*3+2]*J[2*3+1]) +
          J[0*3+1]*(J[1*3+2]*J[2*3+0] - J[1*3+0]*J[2*3+2]) +
          J[0*3+2]*(J[1*3+0]*J[2*3+1] - J[1*3+1]*J[2*3+0]);
      }
      if (invJ) {
        invDet  = 1.0/detJ;
        invJ[0*3+0] = invDet*(J[1*3+1]*J[2*3+2] - J[1*3+2]*J[2*3+1]);
        invJ[0*3+1] = invDet*(J[1*3+2]*J[2*3+0] - J[1*3+0]*J[2*3+2]);
        invJ[0*3+2] = invDet*(J[1*3+0]*J[2*3+1] - J[1*3+1]*J[2*3+0]);
        invJ[1*3+0] = invDet*(J[0*3+1]*J[2*3+2] - J[0*3+2]*J[2*3+1]);
        invJ[1*3+1] = invDet*(J[0*3+2]*J[2*3+0] - J[0*3+0]*J[2*3+2]);
        invJ[1*3+2] = invDet*(J[0*3+0]*J[2*3+1] - J[0*3+1]*J[2*3+0]);
        invJ[2*3+0] = invDet*(J[0*3+1]*J[1*3+2] - J[0*3+2]*J[1*3+1]);
        invJ[2*3+1] = invDet*(J[0*3+2]*J[1*3+0] - J[0*3+0]*J[1*3+2]);
        invJ[2*3+2] = invDet*(J[0*3+0]*J[1*3+1] - J[0*3+1]*J[1*3+0]);
      }
    };
    void computeElementGeometry(const Obj<real_section_type>& coordinates, const point_type& e, double v0[], double J[], double invJ[], double& detJ) {
      if (this->_dim == 2) {
        computeTriangleGeometry(coordinates, e, v0, J, invJ, detJ);
      } else if (this->_dim == 3) {
        computeTetrahedronGeometry(coordinates, e, v0, J, invJ, detJ);
      } else {
        throw ALE::Exception("Unsupport dimension for element geometry computation");
      }
    };
    double getMaxVolume() {
      const topology_type::sheaf_type& patches = this->getTopology()->getPatches();
      double v0[3], J[9], invJ[9], detJ, maxVolume = 0.0;

      for(topology_type::sheaf_type::const_iterator p_iter = patches.begin(); p_iter != patches.end(); ++p_iter) {
        const Obj<real_section_type>&             coordinates = this->getRealSection("coordinates");
        const Obj<topology_type::label_sequence>& cells       = this->getTopology()->heightStratum(p_iter->first, 0);

        for(topology_type::label_sequence::iterator c_iter = cells->begin(); c_iter != cells->end(); ++c_iter) {
          this->computeElementGeometry(coordinates, *c_iter, v0, J, invJ, detJ);
          maxVolume = std::max(maxVolume, detJ);
        }
      }
      return maxVolume;
    };
    // Find the cell in which this point lies (stupid algorithm)
    point_type locatePoint_2D(const patch_type& patch, const real_section_type::value_type point[]) {
      const Obj<real_section_type>&             coordinates = this->getRealSection("coordinates");
      const Obj<topology_type::label_sequence>& cells       = this->getTopology()->heightStratum(patch, 0);
      const int                                 embedDim    = 2;
      double v0[2], J[4], invJ[4], detJ;

      for(topology_type::label_sequence::iterator c_iter = cells->begin(); c_iter != cells->end(); ++c_iter) {
        this->computeElementGeometry(coordinates, *c_iter, v0, J, invJ, detJ);
        double xi   = invJ[0*embedDim+0]*(point[0] - v0[0]) + invJ[0*embedDim+1]*(point[1] - v0[1]);
        double eta  = invJ[1*embedDim+0]*(point[0] - v0[0]) + invJ[1*embedDim+1]*(point[1] - v0[1]);

        if ((xi >= 0.0) && (eta >= 0.0) && (xi + eta <= 1.0)) {
          return *c_iter;
        }
      }
      throw ALE::Exception("Could not locate point");
    };
    //   Assume a simplex and 3D
    point_type locatePoint_3D(const patch_type& patch, const real_section_type::value_type point[]) {
      const Obj<real_section_type>&             coordinates = this->getRealSection("coordinates");
      const Obj<topology_type::label_sequence>& cells       = this->getTopology()->heightStratum(patch, 0);
      const int                                 embedDim    = 3;
      double v0[3], J[9], invJ[9], detJ;

      for(topology_type::label_sequence::iterator c_iter = cells->begin(); c_iter != cells->end(); ++c_iter) {
        this->computeElementGeometry(coordinates, *c_iter, v0, J, invJ, detJ);
        double xi   = invJ[0*embedDim+0]*(point[0] - v0[0]) + invJ[0*embedDim+1]*(point[1] - v0[1]) + invJ[0*embedDim+2]*(point[2] - v0[2]);
        double eta  = invJ[1*embedDim+0]*(point[0] - v0[0]) + invJ[1*embedDim+1]*(point[1] - v0[1]) + invJ[1*embedDim+2]*(point[2] - v0[2]);
        double zeta = invJ[2*embedDim+0]*(point[0] - v0[0]) + invJ[2*embedDim+1]*(point[1] - v0[1]) + invJ[2*embedDim+2]*(point[2] - v0[2]);

        if ((xi >= 0.0) && (eta >= 0.0) && (zeta >= 0.0) && (xi + eta + zeta <= 1.0)) {
          return *c_iter;
        }
      }
      throw ALE::Exception("Could not locate point");
    };
    point_type locatePoint(const patch_type& patch, const real_section_type::value_type point[]) {
      if (this->_dim == 2) {
        return locatePoint_2D(patch, point);
      } else if (this->_dim == 3) {
        return locatePoint_3D(patch, point);
      } else {
        throw ALE::Exception("No point location for mesh dimension");
      }
    };
  public: // BC values for PCICE
    const bc_value_type& getBCValue(const int bcFunc) {
      return this->_bcValues[bcFunc];
    };
    void setBCValue(const int bcFunc, const bc_value_type& value) {
      this->_bcValues[bcFunc] = value;
    };
    bc_values_type& getBCValues() {
      return this->_bcValues;
    };
    void distributeBCValues() {
      int size = this->_bcValues.size();

      MPI_Bcast(&size, 1, MPI_INT, 0, this->comm()); 
      if (this->commRank()) {
        for(int bc = 0; bc < size; ++bc) {
          int           funcNum;
          bc_value_type funcVal;

          MPI_Bcast((void *) &funcNum, 1, MPI_INT,    0, this->comm());
          MPI_Bcast((void *) &funcVal, 4, MPI_DOUBLE, 0, this->comm());
          this->_bcValues[funcNum] = funcVal;
        }
      } else {
        for(bc_values_type::iterator bc_iter = this->_bcValues.begin(); bc_iter != this->_bcValues.end(); ++bc_iter) {
          const int&           funcNum = bc_iter->first;
          const bc_value_type& funcVal = bc_iter->second;
          MPI_Bcast((void *) &funcNum, 1, MPI_INT,    0, this->comm());
          MPI_Bcast((void *) &funcVal, 4, MPI_DOUBLE, 0, this->comm());
        }
      }
    };
  public: // BC values for PyLith
    const Obj<foliated_section_type>& getBoundariesNew() {
      if (this->_boundaries.isNull()) {
        this->_boundaries = new foliated_section_type(this->getTopology());
      }
      return this->_boundaries;
    };
  public: // Discretization
    void setupField(const Obj<real_section_type>& s) {
      const std::string& name  = this->_boundaryCondition->getLabelName();
      const patch_type   patch = 0;

      for(int d = 0; d < this->_dim; ++d) {
        s->setFiberDimensionByDepth(patch, d, this->_discretization->getNumDof(d));
      }
      if (!name.empty()) {
        const Obj<topology_type::label_sequence>& boundary = this->_topology->getLabelStratum(patch, name, 1);

        for(topology_type::label_sequence::iterator e_iter = boundary->begin(); e_iter != boundary->end(); ++e_iter) {
          s->setFiberDimension(patch, *e_iter, -this->_discretization->getNumDof(this->_topology->depth(patch, *e_iter)));
        }
      }
      s->allocate();
      if (!name.empty()) {
        const Obj<real_section_type>&             coordinates = this->getRealSection("coordinates");
        const Obj<topology_type::label_sequence>& boundary    = this->_topology->getLabelStratum(patch, name, 1);

        for(topology_type::label_sequence::iterator e_iter = boundary->begin(); e_iter != boundary->end(); ++e_iter) {
          const real_section_type::value_type *coords = coordinates->restrictPoint(patch, *e_iter);
          const PetscScalar                    value  = this->_boundaryCondition->evaluate(coords);

          s->updateBC(patch, *e_iter, &value);
        }
      }
    };
  public:
    void view(const std::string& name, MPI_Comm comm = MPI_COMM_NULL) {
      if (comm == MPI_COMM_NULL) {
        comm = this->comm();
      }
      if (name == "") {
        PetscPrintf(comm, "viewing a Mesh\n");
      } else {
        PetscPrintf(comm, "viewing Mesh '%s'\n", name.c_str());
      }
      this->getTopology()->view("mesh topology", comm);
      Obj<std::set<std::string> > sections = this->getRealSections();

      for(std::set<std::string>::iterator name = sections->begin(); name != sections->end(); ++name) {
        this->getRealSection(*name)->view(*name);
      }
      sections = this->getIntSections();
      for(std::set<std::string>::iterator name = sections->begin(); name != sections->end(); ++name) {
        this->getIntSection(*name)->view(*name);
      }
      sections = this->getPairSections();
      for(std::set<std::string>::iterator name = sections->begin(); name != sections->end(); ++name) {
        this->getPairSection(*name)->view(*name);
      }
    };
    template<typename value_type>
    static std::string printMatrix(const std::string& name, const int rows, const int cols, const value_type matrix[], const int rank = -1)
    {
      ostringstream output;
      ostringstream rankStr;

      if (rank >= 0) {
        rankStr << "[" << rank << "]";
      }
      output << rankStr.str() << name << " = " << std::endl;
      for(int r = 0; r < rows; r++) {
        if (r == 0) {
          output << rankStr.str() << " /";
        } else if (r == rows-1) {
          output << rankStr.str() << " \\";
        } else {
          output << rankStr.str() << " |";
        }
        for(int c = 0; c < cols; c++) {
          output << " " << matrix[r*cols+c];
        }
        if (r == 0) {
          output << " \\" << std::endl;
        } else if (r == rows-1) {
          output << " /" << std::endl;
        } else {
          output << " |" << std::endl;
        }
      }
      return output.str();
    };
  };

  class MeshBuilder {
  public:
    #undef __FUNCT__
    #define __FUNCT__ "createSquareBoundary"
    /*
      Simple square boundary:

     18--5-17--4--16
      |     |     |
      6    10     3
      |     |     |
     19-11-20--9--15
      |     |     |
      7     8     2
      |     |     |
     12--0-13--1--14
    */
    static Obj<Mesh> createSquareBoundary(const MPI_Comm comm, const double lower[], const double upper[], const int edges[], const int debug = 0) {
      Obj<Mesh> mesh        = new ALE::Mesh(comm, 1, debug);
      int       numVertices = (edges[0]+1)*(edges[1]+1);
      int       numEdges    = edges[0]*(edges[1]+1) + (edges[0]+1)*edges[1];
      double   *coords      = new double[numVertices*2];
      const Obj<Mesh::sieve_type>           sieve    = new ALE::Mesh::sieve_type(mesh->comm(), mesh->debug());
      const Obj<Mesh::topology_type>        topology = new ALE::Mesh::topology_type(mesh->comm(), mesh->debug());
      Mesh::point_type                     *vertices = new Mesh::point_type[numVertices];
      const Mesh::topology_type::patch_type patch    = 0;
      int                                   order    = 0;

      topology->setPatch(patch, sieve);
      mesh->setTopology(topology);
      const Obj<ALE::Mesh::topology_type::patch_label_type>& markers = topology->createLabel(patch, "marker");
      if (mesh->commRank() == 0) {
        /* Create topology and ordering */
        for(int v = numEdges; v < numEdges+numVertices; v++) {
          vertices[v-numEdges] = ALE::Mesh::point_type(v);
        }
        for(int vy = 0; vy <= edges[1]; vy++) {
          for(int ex = 0; ex < edges[0]; ex++) {
            ALE::Mesh::point_type edge(vy*edges[0] + ex);
            int vertex = vy*(edges[0]+1) + ex;

            sieve->addArrow(vertices[vertex+0], edge, order++);
            sieve->addArrow(vertices[vertex+1], edge, order++);
            if ((vy == 0) || (vy == edges[1])) {
              topology->setValue(markers, edge, 1);
              topology->setValue(markers, vertices[vertex], 1);
              if (ex == edges[0]-1) {
                topology->setValue(markers, vertices[vertex+1], 1);
              }
            }
          }
        }
        for(int vx = 0; vx <= edges[0]; vx++) {
          for(int ey = 0; ey < edges[1]; ey++) {
            ALE::Mesh::point_type edge(vx*edges[1] + ey + edges[0]*(edges[1]+1));
            int vertex = ey*(edges[0]+1) + vx;

            sieve->addArrow(vertices[vertex],            edge, order++);
            sieve->addArrow(vertices[vertex+edges[0]+1], edge, order++);
            if ((vx == 0) || (vx == edges[0])) {
              topology->setValue(markers, edge, 1);
              topology->setValue(markers, vertices[vertex], 1);
              if (ey == edges[1]-1) {
                topology->setValue(markers, vertices[vertex+edges[0]+1], 1);
              }
            }
          }
        }
      }
      sieve->stratify();
      topology->stratify();
      for(int vy = 0; vy <= edges[1]; ++vy) {
        for(int vx = 0; vx <= edges[0]; ++vx) {
          coords[(vy*(edges[0]+1)+vx)*2+0] = lower[0] + ((upper[0] - lower[0])/edges[0])*vx;
          coords[(vy*(edges[0]+1)+vx)*2+1] = lower[1] + ((upper[1] - lower[1])/edges[1])*vy;
        }
      }
      ALE::New::SieveBuilder<ALE::Mesh::sieve_type>::buildCoordinates(mesh->getRealSection("coordinates"), mesh->getDimension()+1, coords);
      return mesh;
    }
    #undef __FUNCT__
    #define __FUNCT__ "createCubeBoundary"
    /*
      Simple cubic boundary:

     30----31-----32
      |     |     |
      |  3  |  2  |
      |     |     |
     27----28-----29
      |     |     |
      |  0  |  1  |
      |     |     |
     24----25-----26
    */
    static Obj<Mesh> createCubeBoundary(const MPI_Comm comm, const double lower[], const double upper[], const int faces[], const int debug = 0) {
      Obj<Mesh> mesh        = new ALE::Mesh(comm, 2, debug);
      int       numVertices = (faces[0]+1)*(faces[1]+1)*(faces[2]+1);
      int       numFaces    = 6;
      double   *coords      = new double[numVertices*3];
      const Obj<Mesh::sieve_type>           sieve    = new ALE::Mesh::sieve_type(mesh->comm(), mesh->debug());
      const Obj<Mesh::topology_type>        topology = new ALE::Mesh::topology_type(mesh->comm(), mesh->debug());
      Mesh::point_type                     *vertices = new Mesh::point_type[numVertices];
      const Mesh::topology_type::patch_type patch    = 0;
      int                                   order    = 0;

      topology->setPatch(patch, sieve);
      mesh->setTopology(topology);
      const Obj<ALE::Mesh::topology_type::patch_label_type>& markers = topology->createLabel(patch, "marker");
      if (mesh->commRank() == 0) {
        /* Create topology and ordering */
        for(int v = numFaces; v < numFaces+numVertices; v++) {
          vertices[v-numFaces] = ALE::Mesh::point_type(v);
          topology->setValue(markers, vertices[v-numFaces], 1);
        }
        {
          // Side 0 (Front)
          ALE::Mesh::point_type face(0);
          sieve->addArrow(vertices[0], face, order++);
          sieve->addArrow(vertices[1], face, order++);
          sieve->addArrow(vertices[2], face, order++);
          sieve->addArrow(vertices[3], face, order++);
          topology->setValue(markers, face, 1);
        }
        {
          // Side 1 (Back)
          ALE::Mesh::point_type face(1);
          sieve->addArrow(vertices[4], face, order++);
          sieve->addArrow(vertices[5], face, order++);
          sieve->addArrow(vertices[6], face, order++);
          sieve->addArrow(vertices[7], face, order++);
          topology->setValue(markers, face, 1);
        }
        {
          // Side 2 (Bottom)
          ALE::Mesh::point_type face(2);
          sieve->addArrow(vertices[5], face, order++);
          sieve->addArrow(vertices[4], face, order++);
          sieve->addArrow(vertices[1], face, order++);
          sieve->addArrow(vertices[0], face, order++);
          topology->setValue(markers, face, 1);
        }
        {
          // Side 3 (Top)
          ALE::Mesh::point_type face(3);
          sieve->addArrow(vertices[3], face, order++);
          sieve->addArrow(vertices[2], face, order++);
          sieve->addArrow(vertices[6], face, order++);
          sieve->addArrow(vertices[7], face, order++);
          topology->setValue(markers, face, 1);
        }
        {
          // Side 4 (Left)
          ALE::Mesh::point_type face(4);
          sieve->addArrow(vertices[1], face, order++);
          sieve->addArrow(vertices[4], face, order++);
          sieve->addArrow(vertices[7], face, order++);
          sieve->addArrow(vertices[2], face, order++);
          topology->setValue(markers, face, 1);
        }
        {
          // Side 5 (Right)
          ALE::Mesh::point_type face(5);
          sieve->addArrow(vertices[5], face, order++);
          sieve->addArrow(vertices[0], face, order++);
          sieve->addArrow(vertices[3], face, order++);
          sieve->addArrow(vertices[6], face, order++);
          topology->setValue(markers, face, 1);
        }
      }
      sieve->stratify();
      topology->stratify();
#if 0
      for(int vz = 0; vz <= edges[2]; ++vz) {
        for(int vy = 0; vy <= edges[1]; ++vy) {
          for(int vx = 0; vx <= edges[0]; ++vx) {
            coords[((vz*(edges[1]+1)+vy)*(edges[0]+1)+vx)*2+0] = lower[0] + ((upper[0] - lower[0])/faces[0])*vx;
            coords[((vz*(edges[1]+1)+vy)*(edges[0]+1)+vx)*2+1] = lower[1] + ((upper[1] - lower[1])/faces[1])*vy;
            coords[((vz*(edges[1]+1)+vy)*(edges[0]+1)+vx)*2+2] = lower[2] + ((upper[2] - lower[2])/faces[2])*vz;
          }
        }
      }
#else
      coords[0*2+0] = lower[0];
      coords[0*2+1] = lower[1];
      coords[0*2+2] = upper[2];
      coords[1*2+0] = upper[0];
      coords[1*2+1] = lower[1];
      coords[1*2+2] = upper[2];
      coords[2*2+0] = upper[0];
      coords[2*2+1] = upper[1];
      coords[2*2+2] = upper[2];
      coords[3*2+0] = lower[0];
      coords[3*2+1] = upper[1];
      coords[3*2+2] = upper[2];
      coords[4*2+0] = upper[0];
      coords[4*2+1] = lower[1];
      coords[4*2+2] = lower[2];
      coords[5*2+0] = lower[0];
      coords[5*2+1] = lower[1];
      coords[5*2+2] = lower[2];
      coords[6*2+0] = lower[0];
      coords[6*2+1] = upper[1];
      coords[6*2+2] = lower[2];
      coords[7*2+0] = upper[0];
      coords[7*2+1] = upper[1];
      coords[7*2+2] = lower[2];
#endif
      ALE::New::SieveBuilder<ALE::Mesh::sieve_type>::buildCoordinates(mesh->getRealSection("coordinates"), mesh->getDimension()+1, coords);
      return mesh;
    }
  };
} // namespace ALE

#endif
