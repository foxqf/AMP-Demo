#pragma once

#include "amp.h"
#include "amp_math.h"
#include "ampvectors.h"
#include "raycommon.h"
#include "geometry.h"
#include <limits>

template<typename fp_t>
class material_storage
{
public:
	enum
	{
		material_max_size = 16,
		material_count = 3
	};

	material_storage() restrict(cpu)
	{
		new(materials + 0) phong<fp_t>(color<fp_t>::red(), color<fp_t>::white(), 16.0f, 0.25f);
		new(materials + 1) phong<fp_t>(color<fp_t>::blue(), color<fp_t>::white(), 16.0f, 0.25f);
		new(materials + 2) checker<fp_t>(0.1f, 0.5f);
	}

	color<fp_t> sample(int material_id, const ray<fp_t>& ray, const vector3<fp_t>& position, const vector3<fp_t>& normal) const restrict(cpu, amp)
	{
		const material_value* m = &materials[material_id];

		const material<fp_t>* p = reinterpret_cast<const material<fp_t>*>(m);
		return p->sample(ray, position, normal);
	}

	fp_t get_reflectiveness(int material_id) const restrict(cpu, amp)
	{
		const material_value* m = &materials[material_id];

		const material<fp_t>* p = reinterpret_cast<const material<fp_t>*>(m);
		return p->reflectiveness;
	}

private:
	struct material_value
	{
		fp_t values[material_max_size];
	} materials[material_count];
};

template<typename fp_t>
class scene_storage
{
public:
	enum
	{
		geometry_max_size = 16,
		geometry_count = 3
	};

	scene_storage() restrict(cpu)
	{
		new(geometries + 2) plane<fp_t>(vector3<fp_t>(0.0f, 1.0f, 0.0f), 0.0f, 2);
		new(geometries + 0) sphere<fp_t>(vector3<fp_t>(-15.0f, 15.0f, -10.0f), 15.0f, 0);
		new(geometries + 1) sphere<fp_t>(vector3<fp_t>(12.0f, 10.0f, -10.0f), 10.0f, 1);
	}

	intersect_result<fp_t> intersect(const ray<fp_t>& ray) const restrict(cpu, amp)
	{
		fp_t z = 0.0f;
		fp_t min_dist = 1.0f / z;
		intersect_result<fp_t> min_result;

		for (int i = 0; i < geometry_count; i++)
		{
			const geometry_object* o = &geometries[i];
			const geometry* g = reinterpret_cast<const geometry*>(o);

			intersect_result<fp_t> result(g->intersect(ray));

			if (result.is_hit && result.distance < min_dist)
			{
				min_dist = result.distance;
				min_result = result;
			}
		}

		return min_result;
	}
private:
	struct geometry_object
	{
		fp_t values[geometry_max_size];
	} geometries[geometry_count];
};

template <typename fp_t>
color<fp_t> reflection(ray<fp_t> i_ray, const scene_storage<fp_t>& scene, const material_storage<fp_t>& materials, int max_reflect) restrict(cpu, amp)
{
	color<fp_t> final_color(0.0f, 0.0f, 0.0f);
	fp_t reflectiveness = 1.0f;

	for (int i = 0; i < max_reflect; i++)
	{
		intersect_result<fp_t> r(scene.intersect(i_ray));

		if (r.is_hit)
		{
			fp_t ref_c = materials.get_reflectiveness(r.material);
			color<fp_t> color(materials.sample(r.material, i_ray, r.position, r.normal));	

			color = color * (1.0f - ref_c);
			
			final_color = final_color + (color * reflectiveness);
			reflectiveness = reflectiveness * ref_c;

			if (reflectiveness > 0.0f)
			{	
				vector3<fp_t> r1 = (r.normal * (-2.0f * r.normal.dot(i_ray.direction))) + i_ray.direction;
				i_ray = ray<fp_t>(r.position, r1);
			}
			else
			{
				break;
			}

		}
		else
		{
			break;
		}
	}

	return final_color;
}

template <typename fp_t>
void render_depth(const Concurrency::array_view<unsigned int, 2>& result)
{
	using namespace Concurrency;

	sphere<fp_t> scene(vector3<fp_t>(0, 10, -10), 10);
	perspective_camera<fp_t> camera(vector3<fp_t>(0, 10, 10), vector3<fp_t>(0, 0, -1), vector3<fp_t>(0, 1, 0), 90);

	const float max_depth = 20.0f;

	const int width = result.extent[1];
	const int height = result.extent[0];

	const int xshift = (width - 640) / 2;
	const int yshift = (height - 640) / 2;

	parallel_for_each(result.extent, [=](index<2> idx) restrict(amp)
	{
		const int x = idx[1];
		const int y = idx[0];

		fp_t sy = 1.0f - static_cast<fp_t>(y - yshift) / 640 ;
		fp_t sx = static_cast<fp_t>(x - xshift) / 640;

		ray<fp_t> ray(camera.generate_ray(sx, sy));
		intersect_result<fp_t> ir = scene.intersect(ray);

		unsigned int r = 0;
		unsigned int g = 0;
		unsigned int b = 0;

		if (ir.is_hit)
		{
			float depth = 255.0f - 255.0f * direct3d::saturate(static_cast<float>(ir.distance / max_depth));

			unsigned int intdepth = static_cast<unsigned int>(depth);
			r = intdepth;
			g = intdepth;
			b = intdepth;
		}

		result[idx] = 0xff000000 | (r << 16) | (g << 8) | b;
	});
}

template <typename fp_t>
void render_normal(const Concurrency::array_view<unsigned int, 2>& result)
{
	using namespace Concurrency;

	sphere<fp_t> scene(vector3<fp_t>(0, 10, -10), 10);
	perspective_camera<fp_t> camera(vector3<fp_t>(0, 10, 10), vector3<fp_t>(0, 0, -1), vector3<fp_t>(0, 1, 0), 90);

	const int width = result.extent[1];
	const int height = result.extent[0];

	const int xshift = (width - 640) / 2;
	const int yshift = (height - 640) / 2;

	parallel_for_each(result.extent, [=](index<2> idx) restrict(amp)
	{
		const int x = idx[1];
		const int y = idx[0];

		fp_t sy = 1.0f - static_cast<fp_t>(y - yshift) / 640 ;
		fp_t sx = static_cast<fp_t>(x - xshift) / 640;

		ray<fp_t> ray(camera.generate_ray(sx, sy));
		intersect_result<fp_t> ir = scene.intersect(ray);

		unsigned int r = 0;
		unsigned int g = 0;
		unsigned int b = 0;

		if (ir.is_hit)
		{
			r = static_cast<unsigned int>((ir.normal.x + 1) * 128);
			g = static_cast<unsigned int>((ir.normal.y + 1) * 128);
			b = static_cast<unsigned int>((ir.normal.z + 1) * 128);
		}

		result[idx] = 0xff000000 | (r << 16) | (g << 8) | b;
	});
}

template <typename fp_t>
void render_material(const Concurrency::array_view<unsigned int, 2>& result)
{
	using namespace Concurrency;

	scene_storage<fp_t> scene;
	perspective_camera<fp_t> camera(vector3<fp_t>(0, 5, 15), vector3<fp_t>(0, 0, -1), vector3<fp_t>(0, 1, 0), 90);

	const int width = result.extent[1];
	const int height = result.extent[0];

	const int xshift = (width - 640) / 2;
	const int yshift = (height - 640) / 2;

	material_storage<fp_t> t;

	parallel_for_each(result.extent, [=](index<2> idx) restrict(amp)
	{
		const int x = idx[1];
		const int y = idx[0];

		fp_t sy = 1.0f - static_cast<fp_t>(y - yshift) / 640 ;
		fp_t sx = static_cast<fp_t>(x - xshift) / 640;

		ray<fp_t> ray(camera.generate_ray(sx, sy));
		intersect_result<fp_t> ir(scene.intersect(ray));

		unsigned int r = 0;
		unsigned int g = 0;
		unsigned int b = 0;

		if (ir.is_hit)
		{
			color<fp_t> color(t.sample(ir.material, ray, ir.position, ir.normal));
			r = static_cast<unsigned int>(direct3d::saturate(color.r) * 255);
			g = static_cast<unsigned int>(direct3d::saturate(color.g) * 255);
			b = static_cast<unsigned int>(direct3d::saturate(color.b) * 255);
		}

		result[idx] = 0xff000000 | (r << 16) | (g << 8) | b;
	});
}

template <typename fp_t>
void render_reflection(const Concurrency::array_view<unsigned int, 2>& result, fp_t phi, fp_t theta, fp_t eyedist, int aa_factor)
{
	using namespace Concurrency;

	fp_t r_theta = (theta - 85) * 3.1415926f / 180.0f;
	fp_t r_phi = (270 - phi) * 3.1415926f / 180.0f;

	fp_t r_theta1 = (theta + 5) * 3.1415926f / 180.0f;

	fp_t cos_phi = cos(r_phi);
	fp_t sin_phi = sin(r_phi);

	fp_t px = sin(r_theta) * cos_phi;
	fp_t pz = sin(r_theta) * sin_phi;
	fp_t py = cos(r_theta);

	fp_t ux = sin(r_theta1) * cos_phi;
	fp_t uz = sin(r_theta1) * sin_phi;
	fp_t uy = cos(r_theta1);

	scene_storage<fp_t> scene;
	perspective_camera<fp_t> camera(vector3<fp_t>(px * eyedist, py * eyedist, pz * eyedist), vector3<fp_t>(-px, -py, -pz), vector3<fp_t>(ux, uy, uz), 46);

	const int width = result.extent[1];
	const int height = result.extent[0];

	const int edge = 640 * aa_factor;

	const int xshift = (width - edge) / 2;
	const int yshift = (height - edge) / 2;

	material_storage<fp_t> t;

	parallel_for_each(result.extent, [=](index<2> idx) restrict(amp)
	{
		const int x = idx[1];
		const int y = idx[0];

		fp_t sy = 1.0f - static_cast<fp_t>(y - yshift) / edge ;
		fp_t sx = static_cast<fp_t>(x - xshift) / edge;

		ray<fp_t> ray(camera.generate_ray(sx, sy));

		unsigned int r = 0;
		unsigned int g = 0;
		unsigned int b = 0;

		color<fp_t> color(reflection(ray, scene, t, 3));
		r = static_cast<unsigned int>(direct3d::saturate(color.r) * 255);
		g = static_cast<unsigned int>(direct3d::saturate(color.g) * 255);
		b = static_cast<unsigned int>(direct3d::saturate(color.b) * 255);

		result[idx] = 0xff000000 | (r << 16) | (g << 8) | b;
	});
}