using System;
using System.Text;
using System.Threading.Tasks;
using System.Runtime.InteropServices;
using csmb;

namespace ConsoleApplication1
{
    class Program
    {
        public static csmb.mb.result_t cb(ushort mid, uint len, ref int data)
        {
            Console.WriteLine("int: mid={0}, val={1}", mid, data);
            intCallbackCount_++;
            return csmb.mb.result_t.ERR_OK;
        }

        public static csmb.mb.result_t cb_double(ushort mid, uint len, ref double data)
        {
            Console.WriteLine("double: mid={0}, val={1}", mid, data);
            doubleCallbackCount_++;
            return csmb.mb.result_t.ERR_OK;
        }

        public static csmb.mb.result_t cb_string(ushort mid, uint len, [MarshalAs(UnmanagedType.LPStr)] string data)
        {
            Console.WriteLine("string: mid={0}, val={1}", mid, data);
            stringCallbackCount_++;
            return csmb.mb.result_t.ERR_OK;
        }

        static void Main(string[] args)
        {
            intCallbackCount_    = 0;
            doubleCallbackCount_ = 0;
            stringCallbackCount_ = 0;

            string s = "test #1";
            csmb.mb.client_t c = new csmb.mb.client_t(s);

            string ss = c.name();
            csmb.mb.result_t e;

            Console.WriteLine("{0}={1}", s, ss);

            e = c.reg(string.Format("type=tcp host={0} port={1}", "127.0.0.1", 3333), "mb_test");
            Console.WriteLine("reg: e={0}", e);

            Console.WriteLine("id={0}", c.id());

            ushort mode;
            ushort mid, mid1, mid2;
            int size = 0;

            mode = (ushort)csmb.mb._mtype_t.MT_EVENT;
            e = c.mopen("TEMP", (ushort)csmb.mb._oflags_t.O_RDWR, out mode, out mid, out size);
            Console.WriteLine("mopen(\"TEMP\"): mid={0}, e={1}", mid, e);

            e = c.subscribe(mid, (int)csmb.mb._sflags_t.SF_PUBLISH, (uint)csmb.mb._cmi_t.CM_MANUAL, cb);
            Console.WriteLine("subscribe(\"TEMP\"): e={0}", e);

            mode = (ushort)csmb.mb._mtype_t.MT_EVENT;

            e = c.mcreate("TEMP-DBL", (ushort)csmb.mb._oflags_t.O_RDWR, mode, out mid1, 0.0);
            Console.WriteLine("mcreate(\"TEMP-DBL\"): mid={0}, e={1}", mid, e);

            e = c.subscribe(mid1, (int)csmb.mb._sflags_t.SF_PUBLISH, (uint)csmb.mb._cmi_t.CM_MANUAL, cb_double);
            Console.WriteLine("subscribe(\"TEMP-DBL\"): e={0}", e);

            mode = (ushort)csmb.mb._mtype_t.MT_EVENT;

            e = c.mcreate("TEMP-STR", (ushort)csmb.mb._oflags_t.O_RDWR, mode, out mid2, "Foo");
            Console.WriteLine("mcreate(\"TEMP-STR\"), mid={0}, e={1}", mid, e);
            
            e = c.subscribe(mid2, (int)csmb.mb._sflags_t.SF_PUBLISH, (uint)csmb.mb._cmi_t.CM_MANUAL, cb_string);
            Console.WriteLine("subscribe(\"TEMP-STR\") e={0}", e);

            e = c.dispatch(true);
            Console.WriteLine("dispatch: e={0}", e);

            for (int i = 0; i < 10; ++i) 
            {
                e = c.publish(mid, i);
                Console.WriteLine("publish(\"TEMP\") e={0}", e);

                e = c.publish(mid1, (double)(i) / 5);
                Console.WriteLine("publish(\"TEMP-DBL\") e={0}", e);

                Console.WriteLine("publish string '{0}'", string.Format("{0}", (double)(i) / 5));
                e = c.publish(mid2, string.Format("{0}", (double)(i) / 5));
                Console.WriteLine("publish(\"TEMP-STR\") e={0}", e);

                e = c.dispatch(1, true);
                Console.WriteLine("dispatch: e={0}", e);

                System.Threading.Thread.Sleep(1);
            }

            e = c.dispatch(1000, true);
            Console.WriteLine("dispatch: e={0}", e);

            System.Threading.Thread.Sleep(6000);

            e = c.dispatch(1000, true);
            Console.WriteLine("dispatch: e={0}", e);

            e = c.mclose(mid);
            Console.WriteLine("mclose(\"TEMP\"): e={0}", e);

            e = c.mclose(mid1);
            Console.WriteLine("mclose(\"TEMP-DBL\"): e={0}", e);

            e = c.mclose(mid2);
            Console.WriteLine("mclose(\"TEMP-STR\"): e={0}", e);

            e = c.munlink("TEMP-DBL");
            Console.WriteLine("munlink(\"TEMP-DBL\"): e={0}", e);

            e = c.munlink("TEMP-STR"); 
            Console.WriteLine("munlink(\"TEMP-STR\"): e={0}", e);

            e = c.unreg();
            Console.WriteLine("unreg: e={0}", e);

            Console.WriteLine("int callback count: {0}", intCallbackCount_);
            Console.WriteLine("double callback count: {0}", doubleCallbackCount_);
            Console.WriteLine("string callback count: {0}", stringCallbackCount_);
        }

        static int intCallbackCount_;
        static int doubleCallbackCount_;
        static int stringCallbackCount_;
    }
}
