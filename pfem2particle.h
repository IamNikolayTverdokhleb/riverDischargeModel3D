#ifndef PFEM2PARTICLE_H
#define PFEM2PARTICLE_H

#define PARTICLES_MOVEMENT_STEPS 3
#define MAX_PARTICLES_PER_CELL_PART 3

#define PROJECTION_FUNCTIONS_DEGREE 1

#include <iostream>
#include <fstream>
#include <cmath>
#include <ctime>
#include <unordered_map>

#include <deal.II/base/tensor.h>
#include <deal.II/base/timer.h>

#include <deal.II/distributed/tria.h>

#include <deal.II/dofs/dof_handler.h>
#include <deal.II/dofs/dof_accessor.h>
#include <deal.II/dofs/dof_tools.h>
#include <deal.II/dofs/dof_renumbering.h>

#include <deal.II/fe/fe_q.h>
#include <deal.II/fe/fe_system.h>
#include <deal.II/fe/fe_values.h>

#include <deal.II/fe/mapping_q1.h>
#include <deal.II/base/subscriptor.h>

using namespace dealii;

class pfem2Particle
{
public:
	pfem2Particle(const Point<3> & location,const Point<3> & reference_location,const unsigned int id);
		
	void set_location (const Point<3> &new_location);
	const Point<3> & get_location () const;
	
	void set_reference_location (const Point<3> &new_reference_location);
	const Point<3> & get_reference_location () const;
	
	unsigned int get_id () const;
	
	void set_tria_position (const int &new_position);
	
	void set_map_position (const std::unordered_multimap<int, pfem2Particle*>::iterator &new_position);
	const std::unordered_multimap<int, pfem2Particle*>::iterator & get_map_position () const;
	
	void set_velocity (const Tensor<1,3> &new_velocity);
	void set_velocity_component (const double value, int component);
	
	const Tensor<1,3> & get_velocity_ext() const;
	void set_velocity_ext (const Tensor<1,3> &new_ext_velocity);
	
	const Tensor<1,3> & get_velocity() const;
	double get_velocity_component(int component) const;

	void set_salinity(const double &new_salinity);
	const double & get_salinity() const;

	Triangulation<3>::cell_iterator get_surrounding_cell(const Triangulation<3> &triangulation) const;
	
	unsigned int find_closest_vertex_of_cell(const typename Triangulation<3>::active_cell_iterator &cell, const Mapping<3> &mapping);
	
private:
	Point<3> location;
	Point<3> reference_location;
	unsigned int id;

	int tria_position;
	std::unordered_multimap<int, pfem2Particle*>::iterator map_position;

	Tensor<1,3> velocity;						 //!< Скорость, которую переносит частица
	Tensor<1,3> velocity_ext;					 //!< Внешняя скорость (с которой частица переносится)
    double salinity;                           //!<Соленость, которую переносит частица
};

class pfem2ParticleHandler
{
public:
	pfem2ParticleHandler(const parallel::distributed::Triangulation<3> &tria, const Mapping<3> &coordMapping);
	~pfem2ParticleHandler();
	
	void clear();
	void clear_particles();
	
	void remove_particle(const pfem2Particle *particle);
	void insert_particle(pfem2Particle *particle, const typename Triangulation<3>::active_cell_iterator &cell);
	
	unsigned int n_global_particles() const;
 
    unsigned int n_global_max_particles_per_cell() const;
 
    unsigned int n_locally_owned_particles() const;
    
    unsigned int n_particles_in_cell(const typename Triangulation<3>::active_cell_iterator &cell) const;
    
    void sort_particles_into_subdomains_and_cells();
    
    std::unordered_multimap<int, pfem2Particle*>::iterator begin();
    std::unordered_multimap<int, pfem2Particle*>::iterator end();
    
    std::unordered_multimap<int, pfem2Particle*>::iterator particles_in_cell_begin(const typename Triangulation<3>::active_cell_iterator &cell);
    std::unordered_multimap<int, pfem2Particle*>::iterator particles_in_cell_end(const typename Triangulation<3>::active_cell_iterator &cell);
    
    std::vector<std::set<typename Triangulation<3>::active_cell_iterator>> vertex_to_cells;
    std::vector<std::vector<Tensor<1,3>>> vertex_to_cell_centers;
    
    void initialize_maps();
    
private:
    SmartPointer<const parallel::distributed::Triangulation<3>, pfem2ParticleHandler> triangulation;
    SmartPointer<const Mapping<3>,pfem2ParticleHandler> mapping;
    
    std::unordered_multimap<int, pfem2Particle*> particles;

    unsigned int global_number_of_particles;
 
    unsigned int global_max_particles_per_cell;
};

class pfem2Solver
{
public:
	pfem2Solver();
	~pfem2Solver();
	
	virtual void build_grid() = 0;
	virtual void setup_system() = 0;
	virtual void assemble_system() = 0;
	virtual void solveVx(bool correction = false) = 0;
	virtual void solveVy(bool correction = false) = 0;
    virtual void solveVz(bool correction = false) = 0;
	virtual void solveP() = 0;
	virtual void output_results(bool predictionCorrection = false) = 0;
	
	/*!
	 * \brief Процедура первоначального "посева" частиц в ячейках сетки
	 * \param quantities Вектор количества частиц в каждом направлении (в терминах локальных координат)
	 */
	void seed_particles(const std::vector < unsigned int > & quantities);
	
	/*!
	 * \brief Коррекция скоростей частиц по скоростям в узлах сетки
	 * 
	 * Скорости частиц не сбрасываются (!). Для каждого узла сетки вычисляется изменение поля скоростей.
	 * Затем для каждой частицы по 4 узлам ячейки, в которой содержится частица, вычисляется изменение скорости (коэффициенты - значения функций формы)
	 * и посчитанное изменение добавляется к имеющейся скорости частицы.
	 */
	void correct_particles_velocities();
	
	/*!
	 * \brief "Раздача" скоростей с частиц на узлы сетки
	 * 
	 * Скорости в узлах обнуляются, после чего для каждого узла накапливается сумма скоростей от частиц (коэффициенты - значения функций формы)
	 * из ячеек, содержащих этот узел, и сумма коэффициентов. Итоговая скорость каждого узла - частное от деления первой суммы на вторую.
	 */
	void distribute_particle_velocities_to_grid();
	
	/*!
	 * \brief Перемещение частиц по известному полю скоростей в узлах
	 * 
	 * Перемещение происходит в форме 10 шагов (с шагом time_step/10). Предварительно в частицах корректируется и запоминается скорость. А затем на каждом шаге
	 * + обновляется информация о ячейке, которой принадлежит каждая частица (на первом шаге - за предварительного вычисления переносимой скорости);
	 * + вычисляется скорость частиц по скоростям в узлах сетки (на первом шаге - за предварительного вычисления переносимой скорости);
	 * + координаты частицы изменяются согласно формулам метода Эйлера.
	 */
	void move_particles();
	
	double time,time_step;							//!< Шаг решения задачи методом конечных элементов
	int timestep_number;
	
	Vector<double> solutionVx, solutionVy, solutionVz, solutionP, correctionVx, correctionVy, correctionVz, predictionVx, predictionVy, predictionVz, solutionSal;	//!< Вектор решения, коррекции и прогноза на текущем шаге по времени
	Vector<double> old_solutionVx, old_solutionVy, old_solutionVz, old_solutionP;		//!< Вектор решения на предыдущем шаге по времени (используется для вычисления разности с текущим и последующей коррекции скоростей частиц)
	
	parallel::distributed::Triangulation<3> tria;
	MappingQ1<3> mapping;
	
	pfem2ParticleHandler particle_handler;
	FE_Q<3>  			 feVx, feVy, feVz, feP;
	FESystem<3> 		 fe;
	DoFHandler<3>        dof_handlerVx, dof_handlerVy, dof_handlerVz, dof_handlerP;
	TimerOutput			 *timer;
	
	std::vector<unsigned int> probeDoFnumbers;
    std::set<unsigned int> boundaryDoFNumbers;	//номера степеней свободы, соленость в которых необходимо принудительно обнулять (место впадения реки)
    std::unordered_map<unsigned int, double> openSeaDoFs;	//номера степеней свободы, в которых надо проверять знак горизонтальной скорости для смены типа ГУ ("открытое море"), и их координата z
	
	std::unordered_map<unsigned int, unsigned int> verticesDoFnumbers;
	
protected:
	void seed_particles_into_cell (const typename DoFHandler<3>::cell_iterator &cell);
	bool check_cell_for_empty_parts (const typename DoFHandler<3>::cell_iterator &cell);
	
	double h;
	
private:	
	std::vector < unsigned int > quantities;
	int particleCount = 0;
	time_t solutionTime, startTime;
	
	unsigned int projection_func_count;
};

bool compare_particle_association(const unsigned int a, const unsigned int b, const Tensor<1,3> &particle_direction, const std::vector<Tensor<1,3> > &center_directions);

#endif // PFEM2PARTICLE_H
